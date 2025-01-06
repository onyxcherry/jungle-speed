#include <iostream>
#include <string>
#include <cstring>
#include <ctime>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <SFML/Graphics.hpp>
#include <thread>
#include <nlohmann/json.hpp>

#define BUFFER_SIZE 1024
#define WINDOW_WIDTH 800
#define WINDOW_LENGHT 600

using json = nlohmann::json;


#define WAIT_MSG "Not enough players to start"
#define ONWER_ELSE "Owner is: "
#define ONWER_YOU "You are the owner"
#define CANT_START "You cant start game if you are not the onwer"
#define ENOUGH_PLAYERS "The game can be started"

struct Lobby {
    int id;
    int num_of_players;
    sf::RectangleShape button;
    sf::Text text;
};

struct InLobby {
    sf::RectangleShape StartButton;
    sf::Text GameInfo; 
    sf::Text Owner; 
    std::string owner_name;
    std::vector<std::string> usernames;
};

bool isClicked(const sf::RectangleShape& button, sf::Vector2i mousePos) {
    return button.getGlobalBounds().contains(sf::Vector2f(mousePos));
};

sf::Text createText(const sf::Font& font, const std::string& str, int size, sf::Vector2f pos) {
    sf::Text text;
    text.setFont(font);
    text.setString(str);
    text.setCharacterSize(size);
    text.setFillColor(sf::Color::White);
    text.setPosition(pos);
    return text;
};

Lobby createLobby(int id, int players, const sf::Font& font, sf::Vector2f position) {
    Lobby lobby;
    lobby.id = id;
    lobby.num_of_players = players;

    // Przycisk "Dołącz"
    lobby.button.setSize(sf::Vector2f(150, 40));
    lobby.button.setFillColor(sf::Color(100, 200, 100));
    lobby.button.setPosition(position.x + 300, position.y);

    // Tekst lobby
    std::ostringstream ss;
    ss << "Lobby " << id << " | Gracze: " << players;
    lobby.text = createText(font, ss.str(), 24, position);

    return lobby;
}
/*
StartButton create_start_button(const sf::Font& font, sf::Vector2f position) {
    StartButton start_btn;

    start_btn.button.setSize(sf::Vector2f(150, 40));
    start_btn.button.setFillColor(sf::Color(100, 200, 100));
    start_btn.button.setPosition(position.x + 300, position.y);

    std::ostringstream ss;
    ss << "Start Game ";
    start_btn.text = createText(font, ss.str(), 12, position);
    return start_btn;
}
*/
class JungleSpeedClient
{
    int client_fd;
    sockaddr_in server_addr;
    std::string username;
    bool in_game;
    bool is_owner;
    std::vector<Lobby> lobbyList;
    InLobby curr_lobby;
    sf::Font font;
    int position_in_game;
    std::vector<sf::Texture> textures;
    sf::Clock clock;
    bool show_warrning = false;

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
        setup_textures();



    }

    bool setup_textures() {
        sf::Texture tex;
        sf::Image image;
        sf::Image scaledImage;

        int img_size = 80;
        scaledImage.create(img_size,img_size);
        if(!image.loadFromFile("img.png")) {
            std::cerr << "Cannot load img" << std::endl;
            return false;
        }

        for(unsigned int x=0;x<img_size;++x) {
            for(unsigned int y=0;y<img_size;++y) {
                sf::Color pixelColor = image.getPixel(
                    x * image.getSize().x / img_size,
                    y * image.getSize().y / img_size
                );
                scaledImage.setPixel(x,y,pixelColor);
            }
        }
        tex.loadFromImage(scaledImage);
        textures.push_back(tex);
        return true;
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

    void in_lobby_screen(sf::RenderWindow &window) {

        window.clear(sf::Color(30, 30, 30));


        sf::Event event;
        sf::Sprite spriteHidden;
        sf::Sprite spriteShown;
        

           // Start Game button
        sf::RectangleShape start_btn;

        start_btn.setSize(sf::Vector2f(150, 60));
        start_btn.setFillColor(sf::Color(169, 169, 169));
        start_btn.setPosition(307, 265);

        sf::Text startText = createText(font, "Start", 18,
                                    sf::Vector2f(355,
                                                285));

        //Showing a flashing message 
        if(show_warrning && clock.getElapsedTime().asSeconds() < 2 && curr_lobby.owner_name == username) {
            sf::Text warning; 
            warning.setFont(font);
            warning.setString("Not enough players, cant start game");
            warning.setCharacterSize(18);
            warning.setFillColor(sf::Color::Red);
            warning.setPosition(200, 200);
            window.draw(warning);
        } else if (show_warrning && clock.getElapsedTime().asSeconds() < 2 && curr_lobby.owner_name != username) {
            sf::Text warning;
            warning.setFont(font);
            warning.setString("You are not an owner, you cant start game");
            warning.setCharacterSize(18);
            warning.setFillColor(sf::Color::Red);
            warning.setPosition(200, 200);
            window.draw(warning);
        } else {
            show_warrning = false;
        }

        spriteHidden.setTexture(textures[0]);
        spriteShown.setTexture(textures[0]);
        while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();

                // Wykrycie kliknięcia
                if (event.type == sf::Event::MouseButtonPressed) {
                            if (event.mouseButton.button == sf::Mouse::Left) {

                                if (isClicked(start_btn, sf::Mouse::getPosition(window))) {

                                    if(curr_lobby.usernames.size() < 4) {
                                        show_warrning = true;
                                        clock.restart();
                                    } else {
                                        start_game();
                                    }
                                }
                                // Pobieramy pozycję kursora
                                sf::Vector2i position = sf::Mouse::getPosition(window);
                                
                                // Wypisujemy pozycję w konsoli
                                std::cout << "Pozycja kliknięcia: (" << position.x << ", " << position.y << ")\n";
                                continue;
                    }
                }
            } 

        float player_gap = 50.f;  // Odstęp między graczami
        float x_offset = 800 / 2; 
        int form_first = 0;
        int y_pos = 0;
        int x_pos = 0;
        int rotation = 0;
        int hidden_card_x = 0;
        int hidden_card_y = 0;
        int shown_card_x = 0;
        int shown_card_y = 0;
        int card_offset = 45;

        int not_digonal_offset = 90;
        int digonal_offset = 90/1.41;

        for(int i=position_in_game; i>=0;i--) {




            get_card_position(i, x_pos, y_pos, rotation,
                     hidden_card_x, hidden_card_y,
                     shown_card_x, shown_card_y);

            
            sf::Text playerText;
            playerText.setFont(font);
            playerText.setString(curr_lobby.usernames[form_first++]);
            
            playerText.setCharacterSize(18);
            playerText.setFillColor(sf::Color::White);
            playerText.setPosition(x_pos, y_pos);
            playerText.setRotation(rotation);

            //Setting cards on screen
            spriteHidden.setPosition(hidden_card_x,hidden_card_y);
            spriteHidden.setRotation(rotation);


            spriteShown.setPosition(shown_card_x,shown_card_y);
            spriteShown.setRotation(rotation);
                      
            window.draw(playerText);
            window.draw(spriteHidden);
            window.draw(spriteShown);
        }

        for(int i=0;i<curr_lobby.usernames.size() - position_in_game - 1;i++) {
            //std::cout << "WSZEDLEM: " << curr_lobby.usernames.size() - position_in_game << std::endl;

            get_card_position(7-i, x_pos, y_pos, rotation,
                     hidden_card_x, hidden_card_y,
                     shown_card_x, shown_card_y);


            //std::cout << "W lobby sa starsi uzytkownicy: " << curr_lobby.usernames.size() - position_in_game << std::endl;
            sf::Text playerText;
            playerText.setFont(font);
            int curr_num = curr_lobby.usernames.size();
            playerText.setString(curr_lobby.usernames[curr_num - 1]);
            
            playerText.setCharacterSize(18);
            playerText.setFillColor(sf::Color::White);
            playerText.setPosition(x_pos, y_pos);
            playerText.setRotation(rotation);
            //Setting cards on screen
            spriteHidden.setPosition(hidden_card_x,hidden_card_y);
            spriteHidden.setRotation(rotation);


            spriteShown.setPosition(shown_card_x,shown_card_y);
            spriteShown.setRotation(rotation);

            window.draw(playerText);
            window.draw(spriteHidden);
            window.draw(spriteShown);
        }


           // Showing if game can be startet
            sf::Text playerNuminfo;
            playerNuminfo.setFont(font);
            playerNuminfo.setFont(font);
            playerNuminfo.setCharacterSize(12);
            playerNuminfo.setFillColor(sf::Color(169, 169, 169)); // Grey color
            playerNuminfo.setPosition(300, 330);
            if(curr_lobby.usernames.size() < 4) {
                playerNuminfo.setString(WAIT_MSG);
                startText.setFillColor(sf::Color::White);

            } else {
                playerNuminfo.setString(ENOUGH_PLAYERS);
                startText.setFillColor(sf::Color(100, 200, 100));
            }

            //Showing if players is Onwer
            sf::Text ownerMsg;
            ownerMsg.setFont(font);
            ownerMsg.setCharacterSize(12);
            ownerMsg.setFillColor(sf::Color(169, 169, 169)); // Grey color
            ownerMsg.setPosition(330, 348);

            if(curr_lobby.owner_name == username) {
                //std::cout << "OWNER: " << curr_lobby.owner_name << std::endl;
                //std::cout << "PLAYER: " << username << std::endl;
                ownerMsg.setString(ONWER_YOU);
            } else {
                //std::cout << "OWNER: " << curr_lobby.owner_name << std::endl;
                //std::cout << "PLAYER: " << username << std::endl;

                std::string info_msg = ONWER_ELSE + curr_lobby.owner_name;
                ownerMsg.setString(info_msg);
            }


            window.draw(start_btn);
            window.draw(startText);

            window.draw(playerNuminfo);
            window.draw(ownerMsg);

         //   window.draw(playerText);
           // window.draw(spriteHidden);
         //   window.draw(spriteShown);
        }
        /*
        for(const std::string &name : curr_lobby.usernames) {
            sf::Text playerText;
            playerText.setFont(font);
            playerText.setString(name);
            playerText.setCharacterSize(24);
            playerText.setFillColor(sf::Color::White);
            playerText.setPosition(x_offset, 300 + i*50);
            window.draw(playerText);
            i++;
        }
*/
    
    void chose_screen(sf::RenderWindow &window) {
        sf::Event event;

        // Join button
        sf::RectangleShape join_btn;

        join_btn.setSize(sf::Vector2f(100, 40));
        join_btn.setFillColor(sf::Color(100, 200, 100));
        join_btn.setPosition(350, 550);
        // Dodanie napisu "Dołącz" na przycisku
        sf::Text StartText = createText(font, "Start", 16,
                                    sf::Vector2f(380,
                                                560));
        while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();

                // Wykrycie kliknięcia
                if (event.type == sf::Event::MouseButtonPressed) {
                    if (event.mouseButton.button == sf::Mouse::Left) {
                        if (isClicked(join_btn, sf::Mouse::getPosition(window))) {
                                std::cout << "Stworzono Lobby " << std::endl;
                                create_game();
                        }
                        for (auto& lobby : lobbyList) {
                            if (isClicked(lobby.button, sf::Mouse::getPosition(window))) {
                                std::cout << "Dołączono do Lobby " << lobby.id << std::endl;
                                join_game_click(lobby.id);
                            }
                        }
                    
                    }
                }
            } 
            // Renderowanie okna
            window.clear(sf::Color(30, 30, 30));


            window.draw(join_btn);
            window.draw(StartText);
            for (const auto& lobby : lobbyList) {
                window.draw(lobby.text);
                window.draw(lobby.button);

                // Dodanie napisu "Dołącz" na przycisku
                sf::Text joinText = createText(font, "Dolacz", 20,
                                            sf::Vector2f(lobby.button.getPosition().x + 30,
                                                        lobby.button.getPosition().y + 5));
                window.draw(joinText);
            }

    }

    void run_window()
    {

        sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_LENGHT), "Wybór Lobby");
        if (!font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) {
            std::cerr << "Nie udalo sie zaladowac czcionki!" << std::endl;
            return;
        }
        while (window.isOpen()) {
            if(in_game) {
                in_lobby_screen(window);
            } else {
                chose_screen(window);
            }
            window.display();
         }            
    }

    void run()
    {
        connect_to_server();
        get_username();

        std::thread receiver([this]() {
            handle_response();
        });        

        receiver.detach();
        list_games();
        while (true) {
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


    std::vector<json> parse_msg (std::string &msg){ 
        
        std::vector<json> result;

        if(msg.find("}{") != std::string::npos) {
            std::cout << "Znaleziono " << std::endl;
            std::istringstream stream(msg);
            std::string root; 

            size_t start;
            size_t end;

            while ((end = msg.find("}{", start)) != std::string::npos)  {
                std::cout << root << " ";
                result.push_back(json::parse(root.substr(start, end - start + 1)));
                start = end + 2;
            }
            return result;
        } else {
            result.push_back(json::parse(msg));
            return result;
        }

    }


    void handle_response() {
        while(true) {
        std::cout << "Zaczynamy odbierac wiadomosc" << std::endl;
        std::string response = receive_message();
        std::cout << "Konczymy odbierac wiadomosc" << std::endl;
        std::cout << "Odebrana wiadomosc: " << response << std::endl;
        //json root;
        std::vector<json> roots;

        if (!response.empty())
        {   
            
            //root = json::parse(response);

            //std::cout << "Server respones: " << root.dump() << std::endl;

            std::cout << "Przed prasem" << std::endl;
            roots = parse_msg(response);
            //std::cout << "Game created with ID: " << root["game_id"].get<int>() << "\n";
            //set_is_owner(true);
           // set_in_game(true);
        }
        else
        {
            std::cout << "No response from server.\n";
            return;
        }

        for(const json &r : roots) {
            json root = r;
        std::cout << "Server respones: " << root.dump() << std::endl;
        std::cout << "Zaczynamy przetwarzac response" << std::endl;
        std::string action = root["response"].get<std::string>();
        std::cout << "Konczymy przetwarzac response" << std::endl;


        std::cout << action << std::endl;
        if(action == "UPDATE_LOBBIES") {
                update_lobbies(root);
            } else if (action == "CREATE_GAME") {
                create_game_respone(root);
            }
            else if (action == "JOIN_GAME") {
                join_game_response(root);
            } else if (action == "IN_LOBBY_UPDATE") {
                std::cout << "Wchodzimy do IN_LOBBY_UPDATE" << std::endl;
                in_lobby_update(root);
                std::cout << "Wychodzimy z IN_LOBBY_UPDATE" << std::endl;
            } else if (action == "START") {
                std::cout << "Game started sucesfully" << std::endl;
            }
        }
        }
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
        send_message(request.dump());
    }


    void update_lobbies(json &root) {
        std::vector<Lobby> temp_lobby_list;
        for (const auto &game : root["games"])
            {
                std::cout << "Game ID: " << game["game_id"].get<int>()
                          << " | Players: " << game["player_count"].get<int>()
                          << " | Started: " << (game["is_started"].get<bool>() ? "Yes" : "No")
                          << "\n";
                temp_lobby_list.push_back(createLobby(game["game_id"].get<int>(), game["player_count"].get<int>(), font, sf::Vector2f(50, 100 + temp_lobby_list.size() * 70)));
        }
        lobbyList = std::move(temp_lobby_list);
    }

    void create_game_respone(json &root) {
        std::cout << "Obecni gracze: " <<root["usernames"].get<std::string>() << std::endl;
        if (root["success"].get<bool>())
        {
            std::cout << "Game created with ID: " << root["game_id"].get<int>() << "\n";
            std::cout << "Position: " << root["position"].get<int>() << "\n";

            setup_usernames(root["usernames"].get<std::string>());
            position_in_game = root["position"].get<int>();
            set_is_owner(true);
            set_in_game(true);
            curr_lobby.owner_name = root["owner"].get<std::string>();
        }
        else
        {
            set_in_game(false);
            set_is_owner(false);
            std::cout << "Game creation failed.\n";
        }
    }

    void in_lobby_update(json &root) {
        setup_usernames(root["usernames"].get<std::string>());
        std::cout << "Po updacie w lobby znajduej sie: " << curr_lobby.usernames.size() << std::endl;
        setup_owner(root["owner"].get<std::string>());

    }

    void create_game()
    {
        json request;
        request["action"] = "CREATE_GAME";
        set_in_game(true);
        set_is_owner(true);
        send_message(request.dump());
    }

    void start_game() {
        json request;
        request["action"] = "START_GAME";
        send_message(request.dump());
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
    }

    void join_game_click(int id)
    {
        json request;
        request["action"] = "JOIN_GAME";
        request["game_id"] = id;
        send_message(request.dump());
    }

   

    void join_game_response(json &root) {
        std::cout << "Obecni gracze: " <<root["usernames"].get<std::string>() << std::endl;
        setup_usernames(root["usernames"].get<std::string>());
        std::cout << "pozycja: " <<root["position"].get<int>() << std::endl;
        position_in_game = root["position"].get<int>();
        curr_lobby.owner_name = root["owner"].get<std::string>();
        set_in_game(true);
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

    void setup_usernames(std::string usernames) {
        std::vector<std::string> words; 
        std::istringstream stream(usernames);
        std::string word; 


        // Dzielimy string na wyrazy, używając strumienia
        while (stream >> word) {
            std::cout << word << " ";
            words.push_back(word); // Dodajemy każdy wyraz do wektora
        }
        std::cout << std::endl;
        curr_lobby.usernames = words;
    }

    void setup_owner(std::string name) {
        std::cout << "Owner is: " << name << std::endl;
        curr_lobby.owner_name = name;
    }

    void get_username() {
        json request;
        request["action"] = "GET_USERNAME";
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
                std::cout << "Nick: " << root["username"].get<std::string>() << "\n";
                username = root["username"].get<std::string>();
            }
        }
        else
        {
            std::cout << "No response from server.\n";
        }
    }


    void get_card_position(int i, int &x_pos, int &y_pos, int &rotation,
                     int &hidden_card_x, int &hidden_card_y,
                     int &shown_card_x, int &shown_card_y) {


        //std::cout << "i: " << i << std::endl;
        if(i== 7) {
            x_pos = 730;
            y_pos = 550;
    


// To change

            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;

            rotation = -45;
        } else if(i==6) {
            x_pos = 750;
            y_pos = 350;
            rotation = -90;


// To change

            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;
        } else if(i==5) {
            x_pos = 750;
            y_pos = 100;
            rotation = -135;


// To change

            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;
        } else if (i==4){
            x_pos = 400;
            y_pos = 50;
            rotation = -180;


// To change

            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;
        } else if (i==3){
            x_pos = 100;
            y_pos = 60;
            rotation = -225;


            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;

        } else if (i==2){
            x_pos = 40;
            y_pos = 250;
            rotation = -270;

            hidden_card_y = 205;
            hidden_card_x = 130;
            shown_card_y = 295;
            shown_card_x = 130;
        } else if (i==1) {
            x_pos = 70;
            y_pos = 500;

            hidden_card_y = 400;
            hidden_card_x = 90;
            shown_card_y = 463;
            shown_card_x = 153;

            rotation = -315;
        } else if (i==0){     
            x_pos = 350;
            y_pos = 550;

            hidden_card_y = 450;
            hidden_card_x = 300;
            shown_card_y = 450;
            shown_card_x = 390;

            rotation = 0;
        };

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
    std::thread t1([&]() { client.run_window(); });
    t1.detach();
    client.run();
    return 0;
}
