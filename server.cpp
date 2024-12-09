#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <stdexcept>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

using json = nlohmann::json;

struct Player
{
    int fd;
    std::string username;
    bool active = true;
    std::chrono::time_point<std::chrono::steady_clock> last_action;
};

struct Game
{
    int id;
    std::vector<Player> players;
    bool is_started = false;
    std::vector<std::string> cards_in_play;
    bool totem_caught = false;
};

class JungleSpeedServer
{
    int server_fd;
    sockaddr_in server_addr;
    std::unordered_map<int, Player> players;
    std::unordered_map<int, Game> games;
    int next_game_id = 1;
    int next_player_id = 1;
    int epoll_fd;

    std::string ip_address;
    uint16_t port;

public:
    JungleSpeedServer(const std::string &ip, uint16_t port)
        : ip_address(ip), port(port) {}

    void start_server()
    {
        setup_server_socket();
        setup_epoll();

        std::cout << "Server listening on " << ip_address << ":" << port << "\n";

        while (true)
        {
            epoll_event events[MAX_EVENTS];
            int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

            // TODO: beware that system's fd number *cannot* be identifier of a client
            // as fd is a first-free number, not unique seq!
            for (int i = 0; i < event_count; ++i)
            {
                if (events[i].data.fd == server_fd)
                {
                    accept_new_connection();
                }
                else
                {
                    handle_client_event(events[i].data.fd);
                }
            }
        }
    }

private:
    void setup_server_socket()
    {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        const int one = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
        {
            perror("Setsockopt failed");
            exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr);

        if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        {
            perror("Bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd, SOMAXCONN) == -1)
        {
            perror("Listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
    }

    void setup_epoll()
    {
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1)
        {
            perror("Epoll creation failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = server_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
        {
            perror("Epoll control failed");
            close(server_fd);
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }
    }

    void accept_new_connection()
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1)
        {
            perror("Accept failed");
            return;
        }

        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1)
        {
            perror("Epoll add client failed");
            close(client_fd);
            return;
        }

        players[client_fd] = {client_fd, "Player" + std::to_string(next_player_id++), true,
                              std::chrono::steady_clock::now()};
        std::cout << "New client connected: FD=" << client_fd << "\n";
    }

    void handle_client_event(int client_fd)
    {
        char buffer[BUFFER_SIZE] = {0};
        // TODO: support splitted message in the stream
        // maybe check if current buf content is a valid JSON message?
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0)
        {
            disconnect_client(client_fd);
            return;
        }

        std::string request(buffer);
        try
        {
            json request_json = json::parse(request);
            process_client_message(client_fd, request_json);
        }
        catch (json::parse_error &e)
        {
            send_error(client_fd, "Invalid JSON format.");
        }
    }

    void disconnect_client(int client_fd)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
        players.erase(client_fd);
        std::cout << "Client disconnected: FD=" << client_fd << "\n";
    }

    void send_message(int client_fd, const json &message)
    {
        std::string serialized_message = message.dump();
        send(client_fd, serialized_message.c_str(), serialized_message.size(), 0);
    }

    void send_error(int client_fd, const std::string &error_message)
    {
        json response = {{"error", error_message}};
        send_message(client_fd, response);
    }

    void process_client_message(int client_fd, const json &message)
    {
        if (!message.contains("action"))
        {
            send_error(client_fd, "Missing 'action' field.");
            return;
        }

        std::string action = message["action"];
        players[client_fd].last_action = std::chrono::steady_clock::now();

        if (action == "LIST_GAMES")
        {
            list_games(client_fd);
        }
        else if (action == "CREATE_GAME")
        {
            create_game(client_fd);
        }
        else if (action == "JOIN_GAME")
        {
            join_game(client_fd, message);
        }
        else if (action == "TURN_CARD")
        {
            turn_card(client_fd, message);
        }
        else if (action == "CATCH_TOTEM")
        {
            catch_totem(client_fd, message);
        }
        else
        {
            send_error(client_fd, "Unknown action.");
        }
    }

    void list_games(int client_fd)
    {
        json response;
        response["action"] = "LIST_GAMES";
        response["games"] = json::array();

        for (const auto &pair : games)
        {
            const Game &game = pair.second;
            json game_info = {{"game_id", game.id},
                              {"player_count", game.players.size()},
                              {"is_started", game.is_started}};
            response["games"].push_back(game_info);
        }

        send_message(client_fd, response);
    }

    void create_game(int client_fd)
    {
        int game_id = next_game_id++;
        games[game_id] = {game_id};
        json response = {{"action", "CREATE_GAME"}, {"game_id", game_id}};
        send_message(client_fd, response);
    }

    void join_game(int client_fd, const json &message)
    {
        if (!message.contains("game_id"))
        {
            send_error(client_fd, "Missing 'game_id' field.");
            return;
        }

        int game_id = message["game_id"];
        if (games.find(game_id) == games.end())
        {
            send_error(client_fd, "Game not found.");
            return;
        }

        Game &game = games[game_id];
        if (game.is_started)
        {
            send_error(client_fd, "Game already started.");
            return;
        }

        game.players.push_back(players[client_fd]);
        json response = {{"action", "JOIN_GAME"}, {"game_id", game_id}};
        send_message(client_fd, response);
    }

    void turn_card(int client_fd, const json &message)
    {
        // Implement game logic for turning cards.
    }

    void catch_totem(int client_fd, const json &message)
    {
        // Implement game logic for catching the totem.
    }
};

int main(int argc, char *argv[])
{
    std::string ip = "127.0.0.1";
    uint16_t port = 8080;

    if (argc > 2)
    {
        ip = argv[1];
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    JungleSpeedServer server(ip, port);
    server.start_server();

    return 0;
}
