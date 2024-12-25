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
#include <algorithm>
#include <memory>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

const std::string msg_end_marker = "\t\t";
const std::string EMPTY_CARD = "";

using json = nlohmann::json;

struct Player
{
    int fd;
    std::string username;
    bool active = true;
    std::string top_card = EMPTY_CARD;
    std::vector<std::string> cards_facing_up{};
    std::vector<std::string> cards_facing_down{};
    // where to put that? In Game or server?
    std::chrono::time_point<std::chrono::steady_clock> last_action;
    std::vector<char> msg_in{};
    std::vector<char> msg_out{};

    void show_state()
    {
        std::cout << "Player (" << fd << ") " << username << " (" << ((active) ? "active" : "not active") << ")" << std::endl;
    }
};

class Game
{
    int id;
    std::vector<std::shared_ptr<Player>> players{};
    bool is_started = false;
    std::vector<std::string> cards_in_play{};
    bool totem_held = false;

public:
    Game(int game_id) // : id(game_id)
    {
        id = game_id;
        cards_in_play = generate_cards();
    }

    bool has_been_started() const
    {
        return is_started;
    }

    bool add_player(std::shared_ptr<Player> &player_ptr)
    {
        if (!has_been_started())
        {
            players.push_back(std::move(player_ptr));
            return true;
        }
        return false;
    }

    int get_identifier() const
    {
        return id;
    }

    int get_players_count() const
    {
        return players.size();
    }

    void start()
    {
        is_started = true;
    }

    std::string turn_card(int player_id)
    {
        if (cards_in_play.size() == 0)
        {
            return EMPTY_CARD;
        }
        std::shared_ptr<Player> &player = get_player(player_id);
        std::string turned_up_card = cards_in_play.back();
        cards_in_play.pop_back();
        player->top_card = turned_up_card;
        return turned_up_card;
    }

    // pair<caught_successfully, should_catch>
    std::pair<bool, bool> catch_totem(int player_id)
    {
        std::shared_ptr<Player> &player = get_player(player_id);
        auto cards_repeats = count_cards_repeats();
        bool should_catch = cards_repeats[player->top_card] > 1;

        if (!totem_held)
        {
            totem_held = true;
            return std::make_pair(true, should_catch);
        }
        return std::make_pair(false, should_catch);
    }

    std::shared_ptr<Player> &get_player(int client_fd)
    {
        auto it = std::find_if(players.begin(), players.end(), [client_fd](const std::shared_ptr<Player> &p)
                               { return p->fd == client_fd; });
        if (it == players.end())
        {
            throw std::runtime_error("Cannot find player in this game!");
        }
        return *it;
    }

private:
    std::vector<std::string> generate_cards(int n = 70)
    {
        return std::vector<std::string>{
            "quadruple_mobius_strip_yellow",
            "quadruple_mobius_strip_purple",
            "quadruple_mobius_strip_red",
            "quadruple_shuriken_purple",
            "quadruple_shuriken_green",
            "quadruple_shuriken_orange",
        };
    }

    std::unordered_map<std::string, int> count_cards_repeats()
    {
        std::unordered_map<std::string, int> cards_repeats;

        for (const auto &player : players)
        {
            if (player->top_card == EMPTY_CARD)
            {
                continue;
            }
            ++cards_repeats[player->top_card];
        }
        return cards_repeats;
    }
};

class JungleSpeedServer
{
    int server_fd;
    sockaddr_in server_addr;

    // <game_number, game_obj>
    std::unordered_map<int, Game> games;

    // <client_fd, game_number>
    std::unordered_map<int, int> players_in_games;
    std::vector<std::shared_ptr<Player>> players_out_of_games;
    int next_game_id = 1;
    int next_player_id = 1;
    int epoll_fd;
    int SERVER_PTR = 1;

    const std::string ip_address;
    const uint16_t port;

public:
    JungleSpeedServer(const std::string &ip, uint16_t port)
        : ip_address(ip), port(port) {}

    ~JungleSpeedServer()
    {
        close(server_fd);
        close(epoll_fd);
    }

    void start_server()
    {
        setup_server_socket();
        setup_epoll();

        std::cout << "Server listening on " << ip_address << ":" << port << "\n";

        while (true)
        {
            epoll_event ee;
            int event_count = epoll_wait(epoll_fd, &ee, 1, -1);

            // TODO: beware that system's fd number *cannot* be identifier of a client
            // as fd is a first-free number, not unique seq!
            if (ee.data.ptr == &SERVER_PTR)
            {
                accept_new_connection();
            }
            else if (ee.events & EPOLLIN)
            {
                Player *player = (Player *)(ee.data.ptr);
                handle_client_event(*player);
            }
            else if (ee.events & EPOLLOUT)
            {
                Player *player = (Player *)(ee.data.ptr);
                send_messages_to_client(*player);
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

        epoll_event ee;
        ee.events = EPOLLIN;
        ee.data.ptr = &SERVER_PTR;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ee) == -1)
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
        std::shared_ptr<Player> player = std::make_shared<Player>();
        player->fd = client_fd;
        player->username = "Player" + std::to_string(next_player_id++);
        player->last_action = std::chrono::steady_clock::now();
        players_out_of_games.push_back(player);

        epoll_event event;
        event.events = EPOLLIN;
        event.data.ptr = player.get();

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1)
        {
            perror("Epoll add client failed");
            close(client_fd);
            return;
        }
        std::cout << "New client connected: FD=" << client_fd << "\n";
    }

    std::string extract_message(std::vector<char> &data)
    {
        auto it = std::search(data.begin(), data.end(), msg_end_marker.begin(), msg_end_marker.end());
        if (it == data.end())
        {
            return "";
        }

        std::string result(data.begin(), it);
        data.erase(data.begin(), it + msg_end_marker.size());
        return result;
    }

    void handle_client_event(Player &player)
    {
        char buffer[BUFFER_SIZE] = {};
        ssize_t bytes_received = recv(player.fd, buffer, BUFFER_SIZE, 0);

        if (bytes_received <= 0)
        {
            std::cout << "[Event handling] Will disconnect client " << player.fd << " as received " << bytes_received << " bytes\n";
            disconnect_client(player);
            return;
        }
        player.msg_in.insert(player.msg_in.end(), buffer, buffer + bytes_received);
        std::string message = extract_message(player.msg_in);

        if (message == "")
        {
            return;
        }

        try
        {
            json request_json = json::parse(message);
            process_client_message(player, request_json);
        }
        catch (json::parse_error &e)
        {
            json error_response = {{"error", "Invalid JSON format."}};
            send_error(player, "UNKNOWN", error_response);
        }
    }

    void send_messages_to_client(Player &player)
    {
        int sent_bytes = send(player.fd, player.msg_out.data(), player.msg_out.size(), 0);
        if (sent_bytes <= 0)
        {
            std::cout << "[Sending] Will disconnect client " << player.fd << " as trying to send returned " << sent_bytes << '\n';
            disconnect_client(player);
            return;
        }
        player.msg_out.erase(player.msg_out.begin(), player.msg_out.begin() + sent_bytes);

        if (player.msg_out.size() == 0)
        {
            epoll_event event;
            event.events = EPOLLIN;
            event.data.ptr = &player;

            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, player.fd, &event) == -1)
            {
                perror("Epoll del for OUT client failed");
                close(player.fd);
                return;
            }
        }
    }

    void disconnect_client(Player &disconnected_player)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, disconnected_player.fd, nullptr);
        close(disconnected_player.fd);

        remove_player_from_waiting_list(disconnected_player.fd);
        players_in_games.erase(disconnected_player.fd);
        std::cout << "Client disconnected: FD=" << disconnected_player.fd << "\n";
    }

    // void send_message(Player &player, const json &message)
    // {
    //     std::string serialized_message = message.dump();
    //     send(player.fd, serialized_message.c_str(), serialized_message.size(), 0);
    // }

    void add_message_to_buffer_to_send(Player &player, const json &message)
    {
        std::string serialized_message = message.dump();
        player.msg_out.insert(player.msg_out.end(), serialized_message.begin(), serialized_message.end());

        epoll_event event;
        event.events = EPOLLIN | EPOLLOUT;
        event.data.ptr = &player;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, player.fd, &event) == -1)
        {
            perror("Epoll add for OUT client failed");
            close(player.fd);
            return;
        }
    }

    void send_success(Player &player, const std::string &response_to, json &response)
    {
        response["response"] = response_to;
        response["success"] = true;
        add_message_to_buffer_to_send(player, response);
    }

    void send_error(Player &player, const std::string &response_to, json &error_response)
    {
        error_response["response"] = response_to;
        error_response["success"] = false;
        add_message_to_buffer_to_send(player, error_response);
    }

    void process_client_message(Player &player, const json &message)
    {
        if (!message.contains("action"))
        {
            json error_response = {{"error", "Missing 'action' field."}};
            send_error(player, "UNKNOWN", error_response);
            return;
        }

        std::string action = message["action"];
        player.last_action = std::chrono::steady_clock::now();

        if (action == "LIST_GAMES")
        {
            json response = list_games();
            send_success(player, action, response);
        }
        else if (action == "CREATE_GAME")
        {
            json response = create_game();
            send_success(player, action, response);
        }
        else if (action == "JOIN_GAME")
        {
            auto [success, response] = join_game(player, message);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else if (action == "TURN_CARD")
        {
            auto [success, response] = turn_card(player);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else if (action == "CATCH_TOTEM")
        {
            auto [success, response] = catch_totem(player);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else
        {
            json error_response = {{"error", "Unknown action."}};
            send_error(player, "UNKNOWN", error_response);
        }
    }

    json list_games()
    {
        json response = {
            {"games", json::array()},
        };

        for (const auto &pair : games)
        {
            const Game &game = pair.second;
            json game_info = {{"game_id", game.get_identifier()},
                              {"player_count", game.get_players_count()},
                              {"is_started", game.has_been_started()}};
            response["games"].push_back(game_info);
        }
        return response;
    }

    json create_game()
    {
        int game_id = next_game_id++;
        Game game(game_id);
        games.insert(std::make_pair(game_id, game));
        json response = {{"game_id", game_id}};
        return response;
    }

    std::pair<bool, json> join_game(Player &player, const json &message)
    {
        if (!message.contains("game_id"))
        {
            json response = {{"error", "Missing 'game_id' field."}};
            return make_pair(false, response);
        }

        int game_id = message["game_id"];
        if (games.find(game_id) == games.end())
        {
            json response = {{"error", "Game not found."}};
            return make_pair(false, response);
        }

        int player_fd_searched_for = player.fd;
        if (players_in_games.find(player_fd_searched_for) != players_in_games.end())
        {
            json response = {{"error", "Already joined to this game."}};
            return make_pair(false, response);
        }

        Game &game = games.at(game_id);
        if (game.has_been_started())
        {
            json response = {{"error"}, {"Game already started."}};
            return make_pair(false, response);
        }

        auto player_it = std::find_if(players_out_of_games.begin(), players_out_of_games.end(), [player_fd_searched_for](const std::shared_ptr<Player> &p)
                                      { return p->fd == player_fd_searched_for; });
        if (player_it == players_out_of_games.end())
        {
            json response = {{"error", "Cannot find the player."}};
            return make_pair(false, response);
        }
        auto player_ptr = *player_it;

        players_in_games.insert(std::make_pair(player.fd, game.get_identifier()));
        game.add_player(player_ptr);
        remove_player_from_waiting_list(player.fd);
        json response = {{"game_id", game_id}};
        return make_pair(true, response);
    }

    std::pair<bool, json> turn_card(Player &player)
    {
        auto player_game_it = players_in_games.find(player.fd);
        if (player_game_it == players_in_games.end())
        {
            json response = {{"error", "Cannot turn card as player is not part of any game."}};
            return make_pair(false, response);
        }

        Game &game = games.at(player_game_it->second);
        std::string card = game.turn_card(player.fd);
        json response = {
            {"card", (card == EMPTY_CARD) ? NULL : card},
        };
        return make_pair(true, response);
    }

    std::pair<bool, json> catch_totem(Player &player)
    {
        auto player_game_it = players_in_games.find(player.fd);
        if (player_game_it == players_in_games.end())
        {
            json response = {{"error", "Cannot catch the totem as player is not part of any game."}};
            return make_pair(false, response);
        }

        Game &game = games.at(player_game_it->second);
        auto [caught, should_catch] = game.catch_totem(player.fd);

        // TODO: add bussiness logic of result of catching totem depending on battle or should (not) catch

        json response = {
            {"caught", caught},
        };
        return make_pair(true, response);
    }

    void remove_player_from_waiting_list(int client_fd)
    {
        players_out_of_games.erase(
            std::remove_if(players_out_of_games.begin(), players_out_of_games.end(), [client_fd](std::shared_ptr<Player> const player)
                           { return player->fd == client_fd; }),
            players_out_of_games.end());
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