#include "sdk.h"
#include "json_loader.h"
#include "request_handler.h"
#include "logger.h"
#include "ticker.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <thread>

using namespace std::literals;
namespace net = boost::asio;

namespace {

    // Запускает функцию fn на n потоках, включая текущий
    template <typename Fn>
    void RunWorkers(unsigned n, const Fn& fn) {
        n = std::max(1u, n);
        std::vector<std::jthread> workers;
        workers.reserve(n - 1);
        // Запускаем n-1 рабочих потоков, выполняющих функцию fn
        while (--n) {
            workers.emplace_back(fn);
        }
        fn();
    }

}  // namespace


struct Args {
    std::string config_file;
    std::string static_folder;
    bool dog_random_spawner = false;
    std::string mileseconds_str;
    std::string savetime_period_mileseconds_str;
    std::string game_state_file_path;
};

[[nodiscard]] std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    namespace po = boost::program_options;

    po::options_description desc{ "Allowed options"s };

    Args args;
    desc.add_options()
        // Добавляем опцию --help и её короткую версию -h
        ("help,h", "produce help message")
        // Параметр --tick-period (-t) задаёт период автоматического обновления игрового состояния в миллисекундах. Если этот параметр указан, каждые N миллисекунд сервер должен обновлять координаты объектов. Если этот параметр не указан, время в игре должно управляться с помощью запроса /api/v1/game/tick к REST API.
        ("tick-period,t", po::value(&args.mileseconds_str)->value_name("milliseconds"s), "set tick period")
        // Параметр --config-file (-c) задаёт путь к конфигурационному JSON-файлу игры.
        ("config-file,c", po::value(&args.config_file)->value_name("file"s), "set config file path")
        // Параметр --www-root (-w) задаёт путь к каталогу со статическими файлами игры.
        ("www-root,w", po::value(&args.static_folder)->value_name("dir"s), "set static files root")
        // Параметр --randomize-spawn-points включает режим, при котором пёс игрока появляется в случайной точке случайно выбранной дороги карты.
        ("randomize-spawn-points", "spawn dogs at random positions")
        ("state-file", po::value(&args.game_state_file_path)->value_name("file"s), "set stat file path")
        ("save-state-period", po::value(&args.savetime_period_mileseconds_str)->value_name("milliseconds"s), "set save period");

    // variables_map хранит значения опций после разбора
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.contains("help"s)) {
        // Если был указан параметр --help, то выводим справку и возвращаем nullopt
        std::cout << desc;
        return std::nullopt;
    }

    // Проверяем наличие опций config файла и static folder
    if (!vm.contains("config-file"s)) {
        throw std::runtime_error("Config file have not been specified"s);
    }
    if (!vm.contains("www-root"s)) {
        throw std::runtime_error("Static content file path is not specified"s);
    }
    if (!vm.contains("tick-period")) {;
        LogParamInfo("tick-period", "Was not set: Game will run in test (manual) mod");
    }
    if (vm.contains("randomize-spawn-points")) {
        args.dog_random_spawner = true;
        LogParamInfo("randomize-spawn-points", "Diabled: Dogs will spawn at the beginning of map");
    }
    if (!vm.contains("state-file")) {
        LogParamInfo("stat-file", "Was not set: Game will run without saves");
        if (vm.contains("save-state-period")) {
            vm.erase("save-state-period");
            LogParamInfo("save-state-period", "Parameter will be ignored: Stat file was not set");
        }
    }
    return args;
}

int main(int argc, const char* argv[]) {

    try {
        // Разбираем параметры командной строки
        auto args_opt = ParseCommandLine(argc, argv);
        if (!args_opt) {
            return EXIT_SUCCESS; // Если был указан --help, завершаем работу
        }

        const auto& args = *args_opt;

        // 1. Загружаем карту из файла и строим модель игры
        model::Game game = json_loader::LoadGame(args.config_file);
        // загрузка состояния игры
        if ((args.game_state_file_path.size() != 0) && (FS::exists(FS::path(args.game_state_file_path)) == true)) {
            try {
                game.SetSaveStateFilePath(args.game_state_file_path);
                game.LoadGameState();
                LogEventInfo("Game loaded from file: ", args.game_state_file_path);
            }
            catch (boost::beast::error_code& e) {
                LogError(e, "Error loading game state file");
                return EXIT_FAILURE;
            }
        }
        // проверка рандомного спавна
        if (args.dog_random_spawner) {
            game.EnableRandomSpawner();
        }
        std::string static_files_root = args.static_folder;
        
        InitLogging();

        // 1.5 Создаем контейнер для сырой информации фронтенда
        rawinfo::FrontendInfo frontend_info = json_loader::LoadRawInfo(args.config_file);

        // 2. Инициализируем io_context и БД
        const unsigned num_threads = std::thread::hardware_concurrency();

        // Подключение к БД.
        const char* db_url = std::getenv("GAME_DB_URL");
        if (!db_url) {
            throw std::runtime_error("GAME_DB_URL is not specified");
        }
        auto conn_pool = std::make_shared< postgres::ConnectionPool >(std::max(1u, num_threads), [db_url] {
            return std::make_shared<pqxx::connection>(db_url);
            });
        try {
            postgres::Database::Init(conn_pool);
            game.SetDBConnectionPool(conn_pool);
        }
        catch (std::exception& e) {
            LogError(e, "postgres::Database::Init exception");
            return EXIT_FAILURE;
        }

        net::io_context ioc(num_threads);

        // 2.5 Инициализируем Ticker
        auto ticker_strand = net::make_strand(ioc);
        using TickerHandler = std::function<void(std::chrono::milliseconds)>;

        std::shared_ptr<Ticker> ticker_game_update;
        if (!args.mileseconds_str.empty()) {
            int ms = std::stoi(args.mileseconds_str);
            std::chrono::milliseconds update_period(ms);
            ticker_game_update = std::make_shared<Ticker>(
                ticker_strand,
                std::chrono::milliseconds(update_period),
                [&game](std::chrono::milliseconds ms) {
                    game.Update(ms);
                });
            ticker_game_update->Start();
        }
        else {
            game.EnableManualTimeControl();
        }

        // 3. Добавляем асинхронный обработчик сигналов SIGINT и SIGTERM
        net::signal_set signals(ioc, SIGINT, SIGTERM);

        // 3.5 Обработка сохранения состояния
        std::shared_ptr<Ticker> state_save_ticker;
        if (!args.game_state_file_path.empty()) {
            game.SetSaveStateFilePath(args.game_state_file_path);

            if (!args.savetime_period_mileseconds_str.empty() && !game.ManualTimeControlMode()) {
                // Устанавливаем период сохранения состояния
                int save_period_ms = std::stoi(args.savetime_period_mileseconds_str);
                std::chrono::milliseconds save_period(save_period_ms);

                state_save_ticker = std::make_shared<Ticker>(
                    ticker_strand,
                    save_period,
                    [&game](std::chrono::milliseconds) {
                        try {
                            game.SaveGameState();
                            LogEventInfo("Game saved", "Game state saved automatically.");
                        }
                        catch (const std::exception& ex) {
                            LogError(ex, "Error during automatic game state saving.");
                        }
                    });
                state_save_ticker->Start();
            }
            else if (!args.savetime_period_mileseconds_str.empty()) {
                game.SetSavePeriod(std::stoi(args.savetime_period_mileseconds_str));
            }
          

            signals.async_wait([
                &ioc, &game, state_save_ticker, &args
            ](const boost::system::error_code& ec, int signal) {
                    if (!ec) {
                        try {
                            game.SaveGameState();
                            LogEventInfo("Game saved", "Game state saved before shutdown.");
                        }
                        catch (const std::exception& ex) {
                            LogError(ex, "Error saving game state during shutdown.");
                        }
                        ioc.stop();
                        LogServerStopped(signal);
                    }
                });
        }
        else {
            // Если state-file не указан, просто регистрируем завершение
            signals.async_wait([
                &ioc
            ](const boost::system::error_code& ec, int signal) {
                    if (!ec) {
                        ioc.stop();
                        LogServerStopped(signal);
                    }
                });
        }

        // 4. Создаём обработчик HTTP-запросов и связываем его с моделью игры и корневым каталогом статических файлов
        http_handler::RequestHandler handler{ game, frontend_info, static_files_root, ioc };
        http_handler::LoggingRequestHandler logging_hangler{ handler };

        // 5. Запустить обработчик HTTP-запросов, делегируя их обработчику запросов
        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;
        http_server::ServeHttp(ioc, { address, port }, [&logging_hangler](auto&& req
            , auto&& send
            , const auto& socket) {
                logging_hangler(std::forward<decltype(req)>(req)
                    , std::forward<decltype(send)>(send)
                    , socket);
            });

        // Эта надпись сообщает тестам о том, что сервер запущен и готов обрабатывать запросы
        LogServerStarted(port, address.to_string());
        // 6. Запускаем обработку асинхронных операций
        RunWorkers(std::max(1u, num_threads), [&ioc] {
            ioc.run();
            });



    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        LogServerStopped(1, std::make_optional<std::string>(ex.what()));
        return EXIT_FAILURE;
    }
}
