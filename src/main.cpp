#include <opencv2/opencv.hpp>
#include <opencv2/objdetect/face.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <algorithm>

// --- MySQL Connector/C++ (paquete libmysqlcppconn-dev) ---
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>

// Datos de conexión (los mismos del docker-compose.yml)
static const char* DB_HOST = "tcp://127.0.0.1:3306";
static const char* DB_USER = "acceso";
static const char* DB_PASS = "acceso123";
static const char* DB_NAME = "control_acceso";

using Reloj = std::chrono::steady_clock;

struct Muestra {
    std::string nombre;
    cv::Mat feat;
};

// ---------------------------------------------------------------
// Galería de rostros (archivo galeria.yml)
// ---------------------------------------------------------------
void guardarGaleria(const std::vector<Muestra>& g) {
    cv::FileStorage fs("../galeria.yml", cv::FileStorage::WRITE);
    fs << "muestras" << "[";
    for (auto& m : g) {
        fs << "{" << "nombre" << m.nombre << "feat" << m.feat << "}";
    }
    fs << "]";
}

std::vector<Muestra> cargarGaleria() {
    std::vector<Muestra> g;
    cv::FileStorage fs("../galeria.yml", cv::FileStorage::READ);
    if (!fs.isOpened()) return g;

    cv::FileNode node = fs["muestras"];
    for (auto it = node.begin(); it != node.end(); ++it) {
        Muestra m;
        (*it)["nombre"] >> m.nombre;
        (*it)["feat"] >> m.feat;
        g.push_back(m);
    }
    return g;
}

// Borra todas las muestras de una persona y devuelve cuántas eliminó
size_t borrarPersona(std::vector<Muestra>& g, const std::string& nombre) {
    size_t antes = g.size();
    g.erase(std::remove_if(g.begin(), g.end(),
            [&](const Muestra& m) { return m.nombre == nombre; }),
            g.end());
    size_t eliminadas = antes - g.size();
    if (eliminadas > 0) guardarGaleria(g);
    return eliminadas;
}

// ---------------------------------------------------------------
// Base de datos MySQL
// Si la BD no está disponible, el sistema sigue reconociendo rostros
// y reintenta conectarse cada 10 segundos.
// ---------------------------------------------------------------
class BaseDatos {
public:
    bool conectar() {
        ultimoIntento = Reloj::now();
        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            con.reset(driver->connect(DB_HOST, DB_USER, DB_PASS));
            con->setSchema(DB_NAME);
            std::cout << "[BD] Conectado a MySQL.\n";
            return true;
        } catch (sql::SQLException& e) {
            std::cerr << "[BD] No se pudo conectar: " << e.what() << "\n";
            con.reset();
            return false;
        }
    }

    // Devuelve el id de la persona (la crea si no existe). 0 = sin BD / error.
    int idPersona(const std::string& nombre) {
        auto it = cache.find(nombre);
        if (it != cache.end()) return it->second;
        if (!asegurarConexion()) return 0;
        try {
            {
                std::unique_ptr<sql::PreparedStatement> ins(con->prepareStatement(
                    "INSERT IGNORE INTO personas (nombre) VALUES (?)"));
                ins->setString(1, nombre);
                ins->executeUpdate();
            }
            std::unique_ptr<sql::PreparedStatement> sel(con->prepareStatement(
                "SELECT id FROM personas WHERE nombre = ?"));
            sel->setString(1, nombre);
            std::unique_ptr<sql::ResultSet> rs(sel->executeQuery());
            if (rs->next()) {
                int id = rs->getInt("id");
                cache[nombre] = id;
                return id;
            }
        } catch (sql::SQLException& e) {
            fallo(e);
        }
        return 0;
    }

    // personaId = 0 -> se guarda NULL (rostro desconocido)
    void registrarAcceso(int personaId, bool permitido, double similitud) {
        if (!asegurarConexion()) return;
        try {
            std::unique_ptr<sql::PreparedStatement> ps(con->prepareStatement(
                "INSERT INTO accesos (persona_id, resultado, similitud) VALUES (?, ?, ?)"));
            if (personaId > 0) ps->setInt(1, personaId);
            else               ps->setNull(1, sql::DataType::INTEGER);
            ps->setString(2, permitido ? "PERMITIDO" : "DENEGADO");
            ps->setDouble(3, similitud);
            ps->executeUpdate();
        } catch (sql::SQLException& e) {
            fallo(e);
        }
    }

private:
    std::unique_ptr<sql::Connection> con;
    std::unordered_map<std::string, int> cache;
    Reloj::time_point ultimoIntento = Reloj::now() - std::chrono::hours(1);

    bool asegurarConexion() {
        if (con) return true;
        if (Reloj::now() - ultimoIntento < std::chrono::seconds(10)) return false;
        return conectar();
    }

    void fallo(sql::SQLException& e) {
        std::cerr << "[BD] Error: " << e.what() << "\n";
        con.reset();          // se reintentará la conexión más adelante
    }
};

// ---------------------------------------------------------------
// Programa principal
// ---------------------------------------------------------------
int main() {
    const double UMBRAL = 0.363;
    const auto ESPERA_REGISTRO = std::chrono::seconds(5);   // evita una fila por fotograma

    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "Error: No se pudo abrir la cámara.\n";
        return 1;
    }

    auto detector = cv::FaceDetectorYN::create(
        "models/face_detection_yunet_2023mar.onnx", "",
        cv::Size(320, 320), 0.9f, 0.3f, 5000);

    auto reconocedor = cv::FaceRecognizerSF::create(
        "models/face_recognition_sface_2021dec.onnx", "");

    auto galeria = cargarGaleria();
    std::cout << "Galería cargada. Muestras registradas: " << galeria.size() << "\n";

    BaseDatos bd;
    bd.conectar();

    // Última vez que se guardó un acceso por identidad ("Jordan", "Desconocido", ...)
    std::unordered_map<std::string, Reloj::time_point> ultimoRegistro;

    cv::Mat frame, faces;

    std::cout << "Sistema listo. Teclas: 'r' registrar rostro, 'd' borrar persona, 'ESC' salir.\n";

    while (cap.read(frame)) {
        detector->setInputSize(frame.size());
        detector->detect(frame, faces);

        for (int i = 0; i < faces.rows; i++) {
            cv::Mat alineado, feat;
            reconocedor->alignCrop(frame, faces.row(i), alineado);
            reconocedor->feature(alineado, feat);
            feat = feat.clone();

            double mejor = -1;
            std::string quien = "Desconocido";

            for (auto& m : galeria) {
                double s = reconocedor->match(feat, m.feat, cv::FaceRecognizerSF::FR_COSINE);
                if (s > mejor) {
                    mejor = s;
                    quien = m.nombre;
                }
            }

            bool permitido = mejor >= UMBRAL;
            std::string identidadFinal = permitido ? quien : "Desconocido";

            // --- Registro del acceso en MySQL (máximo uno cada 5 s por identidad) ---
            auto ahora = Reloj::now();
            auto previo = ultimoRegistro.find(identidadFinal);
            if (previo == ultimoRegistro.end() || ahora - previo->second >= ESPERA_REGISTRO) {
                ultimoRegistro[identidadFinal] = ahora;
                int pid = permitido ? bd.idPersona(identidadFinal) : 0;
                bd.registrarAcceso(pid, permitido, mejor);
                std::cout << "[ACCESO] " << (permitido ? "PERMITIDO: " : "DENEGADO: ")
                          << identidadFinal << " (similitud " << mejor << ")\n";
            }

            // --- Dibujo en pantalla ---
            cv::Rect box(
                (int)faces.at<float>(i, 0), (int)faces.at<float>(i, 1),
                (int)faces.at<float>(i, 2), (int)faces.at<float>(i, 3)
            );

            cv::Scalar color = permitido ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::rectangle(frame, box, color, 2);

            std::string texto = permitido ? quien + " (" + std::to_string(mejor).substr(0, 4) + ")" : "DENEGADO";
            cv::putText(frame, texto, {box.x, box.y - 8}, cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }

        cv::imshow("Control de Acceso - Biometria", frame);

        int k = cv::waitKey(1);
        if (k == 27) break; // ESC para salir

        // Tecla 'r': registrar nuevo usuario si hay exactamente 1 rostro en pantalla
        if (k == 'r' && faces.rows == 1) {
            std::string nombre;
            std::cout << "\nIntroduce el nombre de la persona a registrar: ";
            std::cin >> nombre;

            int capturadas = 0;
            std::cout << "Capturando 10 muestras... Mueve ligeramente la cabeza.\n";

            while (capturadas < 10 && cap.read(frame)) {
                detector->setInputSize(frame.size());
                detector->detect(frame, faces);

                if (faces.rows != 1) continue; // Asegurar que solo hay un rostro

                cv::Mat alineado, feat;
                reconocedor->alignCrop(frame, faces.row(0), alineado);
                reconocedor->feature(alineado, feat);

                galeria.push_back({nombre, feat.clone()});
                capturadas++;

                cv::putText(frame, "Registrando... " + std::to_string(capturadas) + "/10",
                            {30, 50}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 0), 2);
                cv::imshow("Control de Acceso - Biometria", frame);
                cv::waitKey(300);
            }

            guardarGaleria(galeria);
            bd.idPersona(nombre);   // crea a la persona en la tabla `personas`
            std::cout << "¡Usuario " << nombre << " registrado con éxito!\n\n";
        }

        // Tecla 'd': borrar a una persona de la galería
        // (su historial de accesos en MySQL se conserva)
        if (k == 'd') {
            std::string nombre;
            std::cout << "\nNombre de la persona a borrar: ";
            std::cin >> nombre;

            size_t eliminadas = borrarPersona(galeria, nombre);
            if (eliminadas > 0)
                std::cout << "Se borró a " << nombre << " (" << eliminadas << " muestras).\n\n";
            else
                std::cout << "No se encontró a nadie con el nombre \"" << nombre
                          << "\" (distingue mayúsculas).\n\n";
        }
    }

    return 0;
}
