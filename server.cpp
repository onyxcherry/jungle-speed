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
#include <set>
#include <unordered_set>

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

bool has_inwards_arrows(std::string card)
{
    return card == "inward_arrows";
}

bool has_outwards_arrows(std::string card)
{
    return card == "outward_arrows";
}

struct Player
{
    int fd;
    std::string username;
    int position_in_game;
    bool active = true;
    bool is_owner = false;
    int game_id = -1;
    std::chrono::time_point<std::chrono::steady_clock> join_lobby_time;
    std::vector<std::string> cards_facing_up{};
    std::vector<std::string> cards_facing_down{};
    // where to put that? In Game or server?
    std::chrono::time_point<std::chrono::steady_clock> last_action;
    std::vector<char> msg_in{};
    std::vector<char> msg_out{};
    std::mutex msg_mtx;

    json get_state()
    {
        json state = {
            {"fd", fd},
            {"username", username},
            {"cards_up", cards_facing_up},
            {"cards_down", cards_facing_down},
        };
        return state;
    }

    std::string get_top_card()
    {
        if (cards_facing_up.size() == 0)
        {
            return EMPTY_CARD;
        }
        return cards_facing_up.back();
    }

    std::string turn_card()
    {
        if (cards_facing_down.size() == 0)
        {
            if (cards_facing_up.size() == 0)
            {
                // player won!
                // Should be handled by other code (before)
                return EMPTY_CARD;
            }
            auto rng = std::default_random_engine{};
            std::vector<std::string> temp{};
            std::move(cards_facing_up.begin(), cards_facing_up.end(), std::back_inserter(temp));
            cards_facing_up.erase(cards_facing_up.begin(), cards_facing_up.end());
            std::shuffle(temp.begin(), temp.end(), rng);
            std::move(temp.begin(), temp.end(), std::back_inserter(cards_facing_down));
        }
        std::string card = cards_facing_down.back();
        cards_facing_down.pop_back();
        cards_facing_up.push_back(card);
        return card;
    }

    // <up count, down count>
    std::pair<int, int> get_cards_count()
    {
        return std::make_pair(cards_facing_up.size(), cards_facing_down.size());
    }

    std::string get_username()
    {
        return username;
    }

    int get_fd()
    {
        return fd;
    }

    int get_position()
    {
        return position_in_game;
    }

    void set_game_id(int id)
    {
        game_id = id;
    };

    int get_game_id()
    {
        return game_id;
    }
};

class Game
{
    int id;
    // <player fd, Player object>; needs to be ordered!
    std::map<int, std::shared_ptr<Player>> players{};
    bool is_started = false;
    std::vector<std::string> cards_in_the_middle{};
    int totem_held_by = NO_TOTEM_HOLDER;

    // TODO: make the variable atomic
    // player fd
    int player_id_turn;
    // <seq, player_id, card>
    std::vector<std::tuple<int, int, std::string>> made_turns{};
    bool next_card_turned_up = false;
    std::unordered_map<std::string, int> cards_repeats{};
    std::string owner_name;

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
            players.insert(make_pair(player_ptr->fd, std::move(player_ptr)));
            return true;
        }
        return false;
    }

    bool remove_player(int player)
    {
        auto it = players.find(player);
        if (it != players.end())
        {
            players.erase(it);
            return true;
        }
        else
        {
            return false;
        }
    }

    void set_owner(std::string name)
    {
        owner_name = name;
    }

    std::string get_owner()
    {
        return owner_name;
    }

    std::vector<std::shared_ptr<Player>> get_players()
    {
        std::vector<std::shared_ptr<Player>> players_to_return{};
        for (const auto &p : players)
        {
            players_to_return.push_back(p.second);
        }
        return players_to_return;
    }

    std::string cards_state(Player &player)
    {
        Player &p = *get_player(player.fd);
        std::string up = p.cards_facing_up.size() > 0 ? "true" : "false";
        std::string down = p.cards_facing_down.size() > 0 ? "true" : "false";
        return up + " " + down;
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
        // TODO: turn on shuffling cards when all added and good working confirmed
        // auto rng = std::default_random_engine{};
        // std::shuffle(deck.begin(), deck.end(), rng);

        auto players_it = players.begin();
        for (const std::string &card : deck)
        {
            players_it->second->cards_facing_down.push_back(card);
            ++players_it;
            if (players_it == players.end())
            {
                players_it = players.begin();
            }
        }
        auto players_list = get_players();
        set_turn_of(players_list[0]->fd);

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
        auto current_player_turn = players.find(player_id_turn);
        ++current_player_turn;
        if (current_player_turn == players.end())
        {
            current_player_turn = players.begin();
        }
        player_id_turn = current_player_turn->first;
    }

    Player &get_current_turn_player()
    {
        return *players[player_id_turn];
    }

    void set_turn_of(int player_id)
    {
        player_id_turn = player_id;
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

    int get_middle_cards_count() const
    {
        return cards_in_the_middle.size();
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
            totem_mtx.unlock();
            totem_cv->notify_all();
            return true;
        }
        return false;
    }

    void put_totem_down()
    {
        totem_held_by = NO_TOTEM_HOLDER;
        totem_mtx.unlock();
    }

    std::shared_ptr<Player> &get_player(int client_fd)
    {
        return players.at(client_fd);
    }

    std::vector<std::shared_ptr<Player>> get_losers(std::string current_card, int winner_id)
    {
        std::vector<std::shared_ptr<Player>> losers{};
        for (const auto &player : get_players())
        {
            if (player->get_top_card() == current_card && player->fd != winner_id)
            {
                losers.push_back(player);
            }
        }
        return losers;
    }

    void transfer_cards_to_losers(std::string current_card, int winner_id)
    {
        std::vector<std::shared_ptr<Player>> losers = get_losers(current_card, winner_id);

        std::vector<std::string> cards_to_be_distributed{};
        Player &winner = *get_player(winner_id);

        std::move(winner.cards_facing_up.begin(), winner.cards_facing_up.end(), std::back_inserter(cards_to_be_distributed));
        winner.cards_facing_up.erase(winner.cards_facing_up.begin(), winner.cards_facing_up.end());
        std::move(cards_in_the_middle.begin(), cards_in_the_middle.end(), std::back_inserter(cards_to_be_distributed));
        cards_in_the_middle.erase(cards_in_the_middle.begin(), cards_in_the_middle.end());

        auto rng = std::default_random_engine{};
        std::shuffle(cards_to_be_distributed.begin(), cards_to_be_distributed.end(), rng);

        auto losers_it = losers.begin();
        for (const std::string &card : cards_to_be_distributed)
        {
            (*losers_it)->cards_facing_down.push_back(card);
            ++losers_it;
            if (losers_it == losers.end())
            {
                losers_it = losers.begin();
            }
        }
    }

    void punish_for_catching_totem_out_of_duel(int player_id)
    {
        Player &punished_player = *get_player(player_id);
        std::vector<std::string> cards_to_be_distributed{};

        std::move(cards_in_the_middle.begin(), cards_in_the_middle.end(), std::back_inserter(cards_to_be_distributed));
        cards_in_the_middle.erase(cards_in_the_middle.begin(), cards_in_the_middle.end());

        for (const auto &player : get_players())
        {
            if (player->cards_facing_up.size() <= 1)
            {
                continue;
            }
            std::move(player->cards_facing_up.begin(), player->cards_facing_up.end() - 1, std::back_inserter(cards_to_be_distributed));
            player->cards_facing_up.erase(player->cards_facing_up.begin(), player->cards_facing_up.end() - 1);
        }

        auto rng = std::default_random_engine{};
        std::shuffle(cards_to_be_distributed.begin(), cards_to_be_distributed.end(), rng);

        std::move(cards_to_be_distributed.begin(), cards_to_be_distributed.end(), std::back_inserter(punished_player.cards_facing_down));
    }

    void transfer_winner_cards_facing_up_to_middle(int player_id)
    {
        Player &player = *get_player(player_id);
        std::move(player.cards_facing_up.begin(), player.cards_facing_up.end(), std::back_inserter(cards_in_the_middle));
        player.cards_facing_up.erase(player.cards_facing_up.begin(), player.cards_facing_up.end());
    }

    std::pair<bool, json> is_ended()
    {
        for (const auto &player : get_players())
        {
            std::pair<int, int> cards_counts = player->get_cards_count();
            if (cards_counts.first == 0 && cards_counts.second == 0)
            {
                json message = {{"Winner", player->fd}};
                return make_pair(true, message);
            }
        }
        json empty_message = json::object();
        return make_pair(false, empty_message);
    }

private:
    std::vector<std::string> generate_cards()
    {
        // TODO; assert that no more than 2 same cards exist
        return std::vector<std::string>{
            "circle_inside_x_red",
            "circle_inside_x_red",
            "circle_inside_x_blue",
            "circle_inside_x_yellow",
            "circle_inside_x_red",
            "circle_inside_x_red",
            "circle_inside_x_yellow",
            "circle_inside_x_yellow",
            "circle_inside_x_yellow",
            "outward_arrows"};
    }

    std::unordered_map<std::string, int> count_cards_repeats()
    {
        std::unordered_map<std::string, int> cards_repeats;

        for (const auto &player : get_players())
        {
            if (player->get_top_card() == EMPTY_CARD)
            {
                continue;
            }
            ++cards_repeats[player->get_top_card()];
        }
        return cards_repeats;
    }

    void add_made_turn(int player_id, std::string &card)
    {
        int seq = made_turns.size() + 1;
        auto turned = std::make_tuple(seq, player_id, card);
        made_turns.push_back(turned);
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
    std::unordered_set<int> players_out_of_games;
    std::unordered_map<int, std::shared_ptr<Player>> all_players;
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

        while (true)
        {
            epoll_event ee;
            int event_count = epoll_wait(epoll_fd, &ee, 1, -1);
            if (event_count == -1)
            {
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }

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
        all_players.insert(std::make_pair(client_fd, player));
        players_out_of_games.insert(client_fd);

        epoll_event event;
        event.events = EPOLLIN;
        event.data.ptr = player.get();

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1)
        {
            perror("Epoll add client failed");
            close(client_fd);
            return;
        }
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
            disconnect_client(player);
            return;
        }
        player.msg_in.insert(player.msg_in.end(), buffer, buffer + bytes_received);

        std::string s(player.msg_in.begin(), player.msg_in.end());
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
        std::lock_guard<std::mutex> lock(player.msg_mtx);
        int sent_bytes = send(player.fd, player.msg_out.data(), player.msg_out.size(), 0);
        if (sent_bytes <= 0)
        {
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

        players_out_of_games.erase(disconnected_player.fd);
        all_players.erase(disconnected_player.fd);
        players_in_games.erase(disconnected_player.fd);
    }

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
        message["response"] = code;
        add_message_to_buffer_to_send(player, message);
    };

    static void send_to_all(std::vector<std::shared_ptr<Player>> players, const std::string &code, json &message)
    {
        for (const auto &player : players)
        {
            send_game_update(*player, code, message);
        }
    }

    static void send_turned_card(std::vector<std::shared_ptr<Player>> players, int player_id, std::string card)
    {
        json turned_card_msg = {{"by", player_id},
                                {"card", card}};
        send_to_all(players, "TURNED_CARD", turned_card_msg);
    }

    static void send_totem_held(std::vector<std::shared_ptr<Player>> players, int player_id)
    {
        json message = {
            {"held", true},
            {"by", player_id},
        };
        send_to_all(players, "TOTEM", message);
    }

    static void send_totem_down(std::vector<std::shared_ptr<Player>> players)
    {
        json totem_down_msg = {{"held", false}};
        send_to_all(players, "TOTEM", totem_down_msg);
    }

    static void send_next_turn(std::vector<std::shared_ptr<Player>> players, std::string player_name)
    {
        json message = {{"next_player", player_name}};
        send_to_all(players, "NEXT_TURN", message);
    }

    static void send_cards_count(const std::shared_ptr<Player> &player, int in_middle_count, Game &game)
    {
        std::pair<int, int> cards_counts = player->get_cards_count();
        json message = {
            {"up", cards_counts.first},
            {"down", cards_counts.second},
            {"middle", in_middle_count},
        };

        for (auto &p : game.get_players())
        {
            message[std::to_string(p->fd)] = game.cards_state(*p);
        }
        send_game_update(*player, "CARDS_COUNTS", message);
    }

    static void send_duel_result(std::vector<std::shared_ptr<Player>> players, int winner_id, std::vector<std::shared_ptr<Player>> losers)
    {
        std::vector<int> losers_ids;
        losers_ids.reserve(losers.size());
        std::transform(losers.begin(), losers.end(), std::back_inserter(losers_ids),
                       [](const std::shared_ptr<Player> &player)
                       { return player->fd; });
        json duel_result_msg = {{"winner", winner_id}, {"losers", losers_ids}};
        send_to_all(players, "DUEL", duel_result_msg);
    }

    void update_lobbies()
    {
        json update = list_games();
        for (const int player_fd : players_out_of_games)
        {
            auto player = all_players.at(player_fd);
            send_success(*player, "UPDATE_LOBBIES", update);
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
            action = "UPDATE_LOBBIES";
            send_success(player, action, response);
        }
        else if (action == "CREATE_GAME")
        {
            auto [success, response] = create_game(player, message);

            // sned info of game to all players out of game
            //(success) ? send_success(player, action, response) : send_error(player, action, response);
            if (success)
            {
                std::cout << "Player created game" << std::endl;
                send_success(player, action, response);
                update_lobbies();
            }
            else
            {
                send_error(player, action, response);
            }
        }
        else if (action == "JOIN_GAME")
        {
            auto [success, response] = join_game(player, message);
            //(success) ? send_success(player, action, response) : send_error(player, action, response);
            if (success)
            {
                send_success(player, action, response);
                action = "IN_LOBBY_UPDATE";
                update_players_in_lobby(player, action, response);
                update_lobbies();
            }
            else
            {
                send_error(player, action, response);
            }
        }
        else if (action == "TURN_CARD")
        {
            auto [success, response] = turn_card(player);
            //(success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else if (action == "CATCH_TOTEM")
        {
            auto [success, response] = catch_totem(player);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else if (action == "GET_USERNAME")
        {
            auto [success, response] = get_username(player);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        }
        else if (action == "START_GAME")
        {
            start_game_by_player(player);
        }
        else if (action == "LEAVE_GAME")
        {
            auto [success, response] = leave_game(player, message);
            (success) ? send_success(player, action, response) : send_error(player, action, response);
        } else if (action == "CREATE_USERNAME") {
            auto [success, response] = create_username(player, message);
            (success) ? send_success(player, action, response) : send_error(player, action, response);

        }
        else
        {
            json error_response = {{"error", "Unknown action."}};
            send_error(player, "UNKNOWN", error_response);
        }
    }


    std::pair<bool, json> create_username(Player &player, const json &message) {
        std::shared_ptr<Player> player_ptr = all_players.at(player.fd);

        if (!message.contains("username"))
        {
            json response = {{"error", "Missing 'game_id' field."}};
            return make_pair(false, response);
        }

        std::string chosen_username = message["username"];

        for (const auto& pair : all_players) {
            std::string temp_username = pair.second->username;
            if(temp_username == chosen_username) {
                json response = {{"info", "Username is already taken"}};
                return make_pair(false, response);
            }
        }
        player_ptr->username = chosen_username;
        json response = {{"username", chosen_username}};
        return make_pair(true, response);
    }



    void start_game(Game &game)
    {
        game.start();
        std::thread t{run, std::ref(game)};
        t.detach();
    }

    std::pair<bool, json> get_username(Player &player)
    {

        json response = {{"username", player.get_username()}, {"id", player.get_fd()}};

        return make_pair(true, response);
    }

    bool check_valid_username(std::string username)
    {
        return std::all_of(username.begin(), username.end(), [](unsigned char c)
                           { return std::isalnum(c); }) &&
               3 <= username.length() && username.length() <= 16;
    }

    void remove_game(int game_id)
    {
        games.erase(game_id);
    }

    static void run(Game &game)
    {

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> no_repeat_dist(3000, 3500);

        json start_info = json::object();
        send_to_all(game.get_players(), "START", start_info);

        // Set players card in beggining
        for (auto &player : game.get_players())
        {
            send_cards_count(player, game.get_middle_cards_count(), game);
        }
        // Code to show player whose turn is it in begining
        send_next_turn(game.get_players(), game.get_current_turn_player().username);

        while (true)
        {
            Player &current_turn_player = game.get_current_turn_player();
            json turn_card_pls_msg = json::object();

            send_game_update(current_turn_player, "CAN_TURN_CARD", turn_card_pls_msg);
            std::string current_card;
            {
                std::unique_lock next_card_lock(game.next_card_mtx);
                game.next_card_cv->wait(next_card_lock, [&game]()
                                        { return game.if_next_card_turned_up(); });

                auto [seq, player_id, card] = game.get_last_made_turn();
                current_card = card;

                send_turned_card(game.get_players(), player_id, card);
                for (auto &player : game.get_players())
                {
                    send_cards_count(player, game.get_middle_cards_count(), game);
                }

                if (has_outwards_arrows(current_card))
                {
                    json one_message = json::object();

                    std::set<std::string> turned_up_cards;
                    bool duel = false;

                    for (const auto &player : game.get_players())
                    {
                        std::string card = player->turn_card();

                        send_cards_count(player, game.get_middle_cards_count(), game);

                        if (turned_up_cards.contains(card))
                        {

                            duel = true;
                        }
                        turned_up_cards.insert(card);

                        one_message[std::to_string(player->fd)] = card;
                    }

                    send_to_all(game.get_players(), "OUTWARDS_ARROWS_TURNED_CARDS", one_message);

                    if (!duel)
                    {

                        game.next_turn();
                        game.set_turn_of(player_id);
                        send_next_turn(game.get_players(), game.get_current_turn_player().username);
                        game.put_totem_down();
                        send_totem_down(game.get_players());
                        continue;
                    }

                    // TODO: if same symbols, continue executing code below (wait for totem, all other players will be losers!)
                    // TODO: if not same symbols, continue the loop from the same player
                }
            }

            auto max_waiting_ms_mistakenly_hold_totem = std::chrono::milliseconds(no_repeat_dist(gen));

            std::unique_lock totem_lock(game.totem_mtx);
            if (game.cards_repeat() && game.totem_cv->wait_for(totem_lock, std::chrono::seconds(5), [&game]()
                                                               { return game.is_totem_held(); }))
            {
                int winner_id = game.get_player_holder_id();
                if (has_inwards_arrows(current_card))
                {
                    game.transfer_winner_cards_facing_up_to_middle(winner_id);
                }
                else
                {
                    game.transfer_cards_to_losers(current_card, winner_id);
                    std::vector<std::shared_ptr<Player>> losers = game.get_losers(current_card, winner_id);

                    send_duel_result(game.get_players(), winner_id, losers);

                    game.set_turn_of(losers.back()->fd);
                }
            }
            else if (game.totem_cv->wait_for(totem_lock, max_waiting_ms_mistakenly_hold_totem, [&game]()
                                             { return game.is_totem_held(); }))
            {
                int player_to_be_punished_id = game.get_player_holder_id();
                game.punish_for_catching_totem_out_of_duel(player_to_be_punished_id);
            }

            for (auto &player : game.get_players())
            {
                send_cards_count(player, game.get_middle_cards_count(), game);
            }

            std::pair<int, json> game_possible_end = game.is_ended();
            if (game_possible_end.first)
            {
                send_to_all(game.get_players(), "END", game_possible_end.second);
                break;
            }

            game.put_totem_down();
            send_totem_down(game.get_players());

            game.next_turn();
            Player &next_turn_player = game.get_current_turn_player();
            send_next_turn(game.get_players(), next_turn_player.username);
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

    std::pair<bool, json> create_game(Player &player, const json &message)
    {

        int games_count = games.size();
        if (games_count >= MAX_GAMES_COUNT)
        {
            json response = {{"error", "Max games count limit hit."}};
            return make_pair(false, response);
        }
/*
        if (!message.contains("username"))
        {
            json response = {{"error", "No username set."}};
            return make_pair(false, response);
        }
        std::string username = message["username"];
*/          
        std::cout << player.username << " Want to create game" << std::endl;
        std::string username = player.username;
        if (!check_valid_username(username))
        {
            json response = {{"error", "The username is disallowed. Only 3-16 alphanumeric characters are allowed."}};
            return make_pair(false, response);
        }

        int game_id = next_game_id++;

        std::shared_ptr game_ptr = std::make_shared<Game>(game_id);
        games.insert(std::make_pair(game_id, game_ptr));

        Game &game = *games.at(game_id);
        int player_fd_searched_for = player.fd;

        if (!players_out_of_games.contains(player_fd_searched_for))
        {
            json response = {{"error", "Player already in the game, cannot create another one."}};
            return make_pair(false, response);
        }

        std::shared_ptr<Player> player_ptr = all_players.at(player_fd_searched_for);
        player_ptr->username = username;

        players_out_of_games.erase(player_fd_searched_for);
        players_in_games.insert(std::make_pair(player.fd, game_id));
        game.add_player(player_ptr);

        game.set_owner(player.username);

        player.join_lobby_time = std::chrono::steady_clock::now();
        player.is_owner = true;
        player.set_game_id(game.get_identifier());

        auto [names, fds] = sort_by_time(game.get_players(), player);
        json response = {{"game_id", game_id}, {"usernames", names}, {"position", player.get_position()}, {"owner", game.get_owner()}, {"fds", fds}};

        return make_pair(true, response);
    }

    std::pair<bool, json> leave_game(Player &player, const json &message)
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
        Game &game = *games.at(game_id);

        std::string user_to_remove_name;
        std::cout << "game owner: " << game.get_owner() << std::endl;
        if (message.contains("username") && player.username == game.get_owner())
        {
            //json response = {{"error", "Missing 'username' field."}};
            //return make_pair(false, response);
            user_to_remove_name = message["username"];
            std::cout << player.username << " Wants to kick: " << user_to_remove_name  << std::endl;
        } else {
            std::cout << player.username << " Want to leave game" << std::endl;
            user_to_remove_name = player.username;
        }

        /*
        for (auto it = all_players.begin(); it != all_players.end(); ++it) {
            std::shared_ptr<Player> player = it->second;  // Wartość (std::shared_ptr<Player>)
            if (player) {  // Sprawdzamy, czy wskaźnik nie jest pusty
                std::cout << "Username: " << player->username << " .Chcenu kiknac: " << user_to_remove_name <<std::endl;  // Dostęp do username
                if(player->username == user_to_remove_name) {
                    std::cout << "zgadza sie" << std::endl;
                }
        }
}
*/
        //for( auto)
/*
        std::cout << player.username << " Want to leave game" << std::endl;

        std::string user_to_remove_name = player.username;
*/
        if (players_in_games.find(player.fd) == players_in_games.end() || players_in_games.find(player.fd)->second != game_id)
        {
            json response = {{"error", "Requestor not in the given game."}};
            return make_pair(false, response);
        }

        auto subject_to_remove = std::find_if(all_players.begin(), all_players.end(), [user_to_remove_name](const std::pair<int, std::shared_ptr<Player>> &p)
                                              { return p.second->get_username() == user_to_remove_name; });
        if (subject_to_remove == all_players.end())
        {
            json response = {{"error", "This player does not exist."}};
            return make_pair(false, response);
        }

        int subject_to_remove_fd = subject_to_remove->second->get_fd();
        if (players_in_games.find(subject_to_remove_fd) == players_in_games.end() || players_in_games.find(subject_to_remove_fd)->second != game_id)
        {
            json response = {{"error", "Subject to remove not in the given game."}};
            return make_pair(false, response);
        }

        if (player.get_username() != user_to_remove_name && player.get_username() != game.get_owner())
        {
            json response = {{"error", "Cannot remove another player from lobby as its non-owner"}};
            return make_pair(false, response);
        }

        auto players_before_removal = game.get_players();
        players_in_games.erase(subject_to_remove_fd);
        game.remove_player(subject_to_remove_fd);
        players_out_of_games.insert(subject_to_remove_fd);

        json removed_info = {{"username", user_to_remove_name}};
        for (const auto &p : players_before_removal)
        {
            send_success(*p, "REMOVED_PLAYER", removed_info);
        }

        if (game.get_players_count() == 0)
        {
            remove_game(game_id);
        }
        else if (game.get_owner() == player.get_username())
        {
            auto players = game.get_players();
            auto longest_playing_it = std::min_element(players.begin(), players.end(),
                                                       [](const std::shared_ptr<Player> &a, const std::shared_ptr<Player> &b)
                                                       {
                                                           return a->join_lobby_time < b->join_lobby_time;
                                                       });
            if (longest_playing_it != players.end())
            {
                game.set_owner((*longest_playing_it)->get_username());
            }
            else
            {
                game.set_owner(players[0]->get_username());
            }
        }

        update_lobbies();

        // *intentional* list of current players due to e.g. `position` field
        // TODO: check or update positions of users in the `sort_by_time` function
        auto [names, fds] = sort_by_time(game.get_players(), player);
        for (const auto &p : game.get_players())
        {
            json update_game_info = {{"game_id", game_id}, {"usernames", names}, {"position", p->get_position()}, {"owner", game.get_owner()}, {"fds", fds}};
            send_success(*p, "IN_LOBBY_UPDATE", update_game_info);
        }

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

/*
        if (!message.contains("username"))
        {
            json response = {{"error", "No username set."}};
            return make_pair(false, response);
        }
        std::string username = message["username"];
*/
        std::cout << player.username << "Wants to join lobby";
        std::string username = player.username;

        if (!check_valid_username(username))
        {
            json response = {{"error", "The username is disallowed. Only 3-16 alphanumeric characters are allowed."}};
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

        players_out_of_games.erase(player_fd_searched_for);
        std::shared_ptr<Player> player_ptr = all_players.at(player_fd_searched_for);

        player_ptr->join_lobby_time = std::chrono::steady_clock::now();
        player_ptr->username = username;

        players_in_games.insert(std::make_pair(player.fd, game.get_identifier()));
        game.add_player(player_ptr);

        auto [names, fds] = sort_by_time(game.get_players(), player);

        player.set_game_id(game.get_identifier());

        json response = {{"game_id", game_id}, {"usernames", names}, {"position", player.get_position()}, {"owner", game.get_owner()}, {"fds", fds}};

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

        Player &current_turn_player = game.get_current_turn_player();
        if (player.fd != current_turn_player.fd)
        {
            json response = {{"error", "Cannot turn card in other player's turn."}};
            return make_pair(false, response);
        }

        if (game.if_next_card_turned_up())
        {
            json response = {{"error", "Cannot turn card again."}};
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
            send_totem_held(game.get_players(), player.fd);
        }
        return make_pair(caught, json::object());
    }

    std::pair<std::string, std::string> sort_by_time(std::vector<std::shared_ptr<Player>> players, Player &main_player)
    {
        std::sort(players.begin(), players.end(), [](const std::shared_ptr<Player> &a, const std::shared_ptr<Player> &b)
                  { return a->join_lobby_time < b->join_lobby_time; });

        std::string result;
        std::string fd;
        int position = 0;
        for (const auto &player : players)
        {
            if (player->fd == main_player.fd)
            {
                main_player.position_in_game = position;
            }
            position++;
            result += player->username;
            fd += std::to_string(player->fd);
            result += " ";
            fd += " ";
        }

        return std::make_pair(result, fd);
    }

    void update_players_in_lobby(Player &main_player, std::string &action, json &response)
    {
        Game &game = *games.at(main_player.get_game_id());
        for (const auto &p : game.get_players())
        {
            if (main_player.fd != p->fd)
            {
                send_success(*p, action, response);
            }
        }
    }
    void start_game_by_player(Player &main_player)
    {
        Game &game = *games.at(main_player.get_game_id());
        if (game.get_owner() == main_player.get_username() && game.get_players().size())
        {
            start_game(game);

            json response = list_games();
            for (const int player_fd : players_out_of_games)
            {
                auto player = all_players.at(player_fd);
                send_success(*player, "UPDATE_LOBBIES", response);
            }
        }
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