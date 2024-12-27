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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include <tuple>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

#define NO_TOTEM_HOLDER -1

const std::string msg_end_marker = "\t\t";
const std::string EMPTY_CARD = "";

const int PLAYERS_COUNT_THRESHOLD = 6;
const int MAX_PLAYERS_COUNT = 8;
const int MAX_GAMES_COUNT = 10;

int epoll_fd;

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
    std::mutex msg_mtx;

    std::string turn_card()
    {
        if (cards_facing_up.size() == 0)
        {
            if (cards_facing_down.size() == 0)
            {
                // player won!
                // Should be handled by other code (before)
                return EMPTY_CARD;
            }
            auto rng = std::default_random_engine{};
            std::shuffle(cards_facing_down.begin(), cards_facing_down.end(), rng);
            std::move(cards_facing_down.begin(), cards_facing_down.end(), std::back_inserter(cards_facing_up));
        }
        std::string card = cards_facing_up.back();
        cards_facing_up.pop_back();
        top_card = card;
        return card;
    }
};

class Game
{
    int id;
    std::vector<std::shared_ptr<Player>> players{};
    bool is_started = false;
    std::vector<std::string> cards_in_the_middle{};
    int totem_held_by = NO_TOTEM_HOLDER;
    int player_idx_turn = 0;
    // <seq, player_id, card>
    std::vector<std::tuple<int, int, std::string>> made_turns{};
    bool can_turn_up_next_card = true;
    bool next_card_turned_up = false;
    std::unordered_map<std::string, int> cards_repeats{};

public:
    // changing totem_held_by needs locked totem_mtx
    std::mutex totem_mtx;
    std::unique_ptr<std::condition_variable> totem_cv = std::make_unique<std::condition_variable>();

    // used for signaling that player made the turn in each round
    // analyzing cards on the table requires totem_mtx to be held!
    std::mutex next_card_mtx;
    std::unique_ptr<std::condition_variable> next_card_cv = std::make_unique<std::condition_variable>();

    Game(int game_id) // : id(game_id)
    {
        id = game_id;
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

    std::vector<std::shared_ptr<Player>> &get_players_to_be_informed()
    {
        return players;
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
        std::vector<std::string> deck = generate_cards();
        auto rng = std::default_random_engine{};
        std::shuffle(deck.begin(), deck.end(), rng);

        for (const std::string &card : deck)
        {
            for (const auto &player : players)
            {
                player->cards_facing_up.push_back(card);
            }
        }

        is_started = true;
    }

    bool if_next_card_turned_up() const
    {
        return next_card_turned_up;
    }

    std::tuple<int, int, std::string> get_last_made_turn()
    {
        return made_turns.back();
    }

    void next_turn()
    {
        next_card_turned_up = false;
        player_idx_turn = (player_idx_turn + 1) % get_players_count();
    }

    Player &get_current_turn_player()
    {
        return *players[player_idx_turn];
    }

    void turn_card(int player_id)
    {
        std::shared_ptr<Player> &player = get_player(player_id);
        {
            std::lock_guard<std::mutex> lock(next_card_mtx);
            std::string turned_up_card = player->turn_card();
            next_card_turned_up = true;
            add_made_turn(player_id, turned_up_card);
            cards_repeats = count_cards_repeats();
        }
        next_card_cv->notify_one();
    }

    void add_made_turn(int player_id, std::string &card)
    {
        int seq = made_turns.size() + 1;
        auto turned = std::make_tuple(seq, player_id, card);
        made_turns.push_back(turned);
    }

    bool cards_repeat()
    {
        return std::any_of(cards_repeats.begin(), cards_repeats.end(), [](const auto &pair)
                           { return pair.second >= 2; });
    }

    bool is_totem_held() const
    {
        return totem_held_by != NO_TOTEM_HOLDER;
    }

    int get_player_holder_id() const
    {
        return totem_held_by;
    }

    bool catch_totem(int player_id)
    {
        if (totem_mtx.try_lock() && !is_totem_held())
        {
            totem_held_by = player_id;
            totem_cv->notify_one();
            return true;
        }
        return false;
    }

    void put_totem_down()
    {
        std::lock_guard<std::mutex> lock(totem_mtx);
        totem_held_by = NO_TOTEM_HOLDER;
        totem_cv->notify_one();
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

    int get_loser_id(std::string current_card, int winner_id)
    {
        for (const auto &player : players)
        {
            if (player->top_card == current_card && player->fd != winner_id)
            {
                return player->fd;
            }
        }
        throw std::runtime_error("Loser id not found!");
    }

private:
    std::vector<std::string> generate_cards(int n = 70)
    {
        // TODO; assert that no more than 2 same cards exist
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
    std::unordered_map<int, std::shared_ptr<Game>> games;

    // <client_fd, game_number>
    std::unordered_map<int, int> players_in_games;
    std::vector<std::shared_ptr<Player>> players_out_of_games;
    int next_game_id = 1;
    int next_player_id = 1;
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

    static void add_message_to_buffer_to_send(Player &player, const json &message)
    {
        std::string serialized_message = message.dump();
        std::lock_guard<std::mutex> lock(player.msg_mtx);
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

    static void send_game_update(Player &player, const std::string &code, json &message)
    {
        message["type"] = "GAME_UPDATE";
        message["code"] = code;
        add_message_to_buffer_to_send(player, message);
    };

    static void send_to_all(std::vector<std::shared_ptr<Player>> &players, const std::string &code, json &message)
    {
        for (const auto &player : players)
        {
            send_game_update(*player, code, message);
        }
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
            auto [success, response] = create_game();
            (success) ? send_success(player, action, response) : send_error(player, action, response);
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

    void start_game(Game &game)
    {
        game.start();
        std::thread t{run, std::ref(game)};
        t.detach();
    }

    static void run(Game &game)
    {
        std::cout << "Run się odpala" << std::endl;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dont_repeat(3000, 3500);
        int notrep_sleeping_duration_ms = dont_repeat(gen);

        json start_info = {};
        send_to_all(game.get_players_to_be_informed(), "START", start_info);

        while (true)
        {
            Player &current_turn_player = game.get_current_turn_player();
            json response = json::object();
            send_game_update(current_turn_player, "CAN_TURN_CARD", response);
            std::string current_card;
            {
                std::unique_lock next_card_lock(game.next_card_mtx);
                game.next_card_cv->wait(next_card_lock, [&game]()
                                        { return game.if_next_card_turned_up(); });

                auto [seq, player_id, card] = game.get_last_made_turn();
                current_card = card;
                json turned_card_message = {{"by", player_id},
                                            {"card", card}};
                send_to_all(game.get_players_to_be_informed(), "TURNED_CARD", turned_card_message);
            }

            std::unique_lock totem_lock(game.totem_mtx);
            if (game.cards_repeat() && game.totem_cv->wait_for(totem_lock, std::chrono::seconds(5), [&game]()
                                                               { return game.is_totem_held(); }))
            {
                int loser_id = game.get_loser_id(current_card, game.get_player_holder_id());
                // TODO: implement rest of logic for loser
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(notrep_sleeping_duration_ms));
            }
            game.put_totem_down();
            game.next_turn();
        }
    }

    json list_games()
    {
        json response = {
            {"games", json::array()},
        };

        for (const auto &pair : games)
        {
            const Game &game = *pair.second;
            json game_info = {{"game_id", game.get_identifier()},
                              {"player_count", game.get_players_count()},
                              {"is_started", game.has_been_started()}};
            response["games"].push_back(game_info);
        }
        return response;
    }

    std::pair<bool, json> create_game()
    {
        int games_count = games.size();
        if (games_count >= MAX_GAMES_COUNT)
        {
            json response = {{"error", "Max games count limit hit."}};
            return make_pair(false, response);
        }
        int game_id = next_game_id++;

        std::shared_ptr game_ptr = std::make_shared<Game>(game_id);
        games.insert(std::make_pair(game_id, game_ptr));
        json response = {{"game_id", game_id}};
        return make_pair(true, response);
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

        Game &game = *games.at(game_id);
        if (game.has_been_started())
        {
            json response = {{"error", "Game already started."}};
            return make_pair(false, response);
        }

        int players_count = game.get_players_count();
        if (players_count >= MAX_PLAYERS_COUNT)
        {
            json response = {{"error", "Max players count limit hit."}};
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

        // TODO: explicitly starting by first player in the game
        if (players_count + 1 == PLAYERS_COUNT_THRESHOLD)
        {
            start_game(game);
        }

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

        Game &game = *games.at(player_game_it->second);
        if (!game.has_been_started())
        {
            json response = {{"error", "Cannot turn card as the game has not been started."}};
            return make_pair(false, response);
        }

        game.turn_card(player.fd);
        json response = json::object();
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

        Game &game = *games.at(player_game_it->second);
        if (!game.has_been_started())
        {
            json response = {{"error", "Cannot catch the totem as the game has not been started."}};
            return make_pair(false, response);
        }

        // ? can the totem be caught when a card is changing?
        // ? any game-bad state possible?
        // ? despite the player's obvious loss

        bool caught = game.catch_totem(player.fd);
        if (caught)
        {
            json message = {
                {"caught", caught},
                {"by", player.fd},
            };
            send_to_all(game.get_players_to_be_informed(), "TOTEM", message);
        }
        return make_pair(caught, json::object());
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