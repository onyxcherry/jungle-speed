#include <iostream>
#include <string>
#include <cstring>
#include <ctime>
#include <chrono>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

using json = nlohmann::json;




class JungleSpeedClient
{
    int client_fd;
    sockaddr_in server_addr;
    std::string username;
    bool in_game;
    bool is_owner;
public:
    JungleSpeedClient(const std::string &ip, uint16_t port)
    {
        client_fd = socket(AF_INET, SOCK_STREAM, 0);
        in_game = false;
        if (client_fd == -1)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);



    }

    void connect_to_server()
    {
        if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        {
            perror("Connection failed");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        std::cout << "Connected to server.\n";
    }

    void send_message(const std::string &message)
    {
        send(client_fd, message.c_str(), message.size(), 0);
    }

    std::string receive_message()
    {
        char buffer[BUFFER_SIZE] = {0};
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0)
        {
            return "";
        }
        buffer[bytes_received] = '\0';
        return std::string(buffer);
    }

    void run()
    {
        connect_to_server();
        while (true)
        {
            int choice;


            if(in_game) {
                display_game_window();
                std::cin >> choice;
                if (choice == 1) {
                    
                } else if (choice == 2) {
                    set_in_game(false);
                };

            } else {    

                display_menu();
                std::cin >> choice;

                if (choice == 1)
                {
                    list_games();
                }
                else if (choice == 2)
                {
                    create_game();
                }
                else if (choice == 3)
                {
                    join_game();
                }
                else if (choice == 4)
                {
                    turn_card();
                }
                else if (choice == 5)
                {
                    catch_totem();
                }
                else if (choice == 0)
                {
                    break;
                }
                else
                {
                    std::cout << "Invalid option. Try again.\n";
                }
            }
        }

        close(client_fd);
    }

private:
    void display_menu()
    {
        std::cout << "\n--- Jungle Speed Client ---\n";
        std::cout << "1. List Games\n";
        std::cout << "2. Create Game\n";
        std::cout << "3. Join Game\n";
        std::cout << "4. Turn Card\n";
        std::cout << "5. Catch Totem\n";
        std::cout << "0. Exit\n";
        std::cout << "Enter your choice: ";
    }

    void display_game_window() {
        std::cout << "\n--- Game window ---\n";
        std::cout << "1. List Players\n";
        std::cout << "2. Exit Game\n";
        std::cout << "3. Start Game\n";
    }

    void list_games()
    {
        json request;
        request["action"] = "LIST_GAMES";

        std::cout << request.dump() << std::endl;

        send_message(request.dump());

        std::string response = receive_message();
        if (!response.empty())
        {
            json root = json::parse(response);
            std::cout << "\nAvailable Games:\n";
            for (const auto &game : root["games"])
            {
                std::cout << "Game ID: " << game["game_id"].get<int>()
                          << " | Players: " << game["player_count"].get<int>()
                          << " | Started: " << (game["is_started"].get<bool>() ? "Yes" : "No")
                          << "\n";
            }
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }

    void create_game()
    {
        json request;
        request["action"] = "CREATE_GAME";
        send_message(request.dump());

        std::string response = receive_message();
        if (!response.empty())
        {
            json root = json::parse(response);
            std::cout << "Game created with ID: " << root["game_id"].get<int>() << "\n";
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }

    void join_game()
    {
        int game_id;
        std::cout << "Enter Game ID to join: ";
        std::cin >> game_id;

        json request;
        request["action"] = "JOIN_GAME";
        request["game_id"] = game_id;
        send_message(request.dump());

        std::string response = receive_message();
        if (!response.empty())
        {
            json root = json::parse(response);
            if (root.contains("error"))
            {
                std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "Successfully joined the game.\n";
                set_in_game(true);
            }
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }

    void turn_card()
    {
        auto timestamp = get_current_timestamp();
        json request;
        request["action"] = "TURN_CARD";
        request["timestamp"] = timestamp;
        send_message(request.dump());

        std::string response = receive_message();
        if (!response.empty())
        {
            json root = json::parse(response);
            if (root.contains("error"))
            {
                std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "Turned card: " << root["card"].get<std::string>() << "\n";
            }
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }

    void catch_totem()
    {
        auto timestamp = get_current_timestamp();
        json request;
        request["action"] = "CATCH_TOTEM";
        request["timestamp"] = timestamp;
        send_message(request.dump());

        std::string response = receive_message();
        if (!response.empty())
        {
            json root = json::parse(response);
            if (root.contains("error"))
            {
                std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "Totem caught!\n";
            }
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }

    std::string get_current_timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return std::to_string(milliseconds);
    }

    void set_in_game(bool state) {
        in_game = state;
    }

    void set_is_owner(bool state) {
        is_owner = state;
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


    JungleSpeedClient client(ip, port);



    client.run();
    return 0;
}
