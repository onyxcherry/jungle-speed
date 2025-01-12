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
#include "json.hpp"

#define BUFFER_SIZE 1024
#define WINDOW_WIDTH 800
#define WINDOW_LENGHT 600
#define MAX_GAMES_COUNT 8
const std::string msg_end_marker = "\t\t";

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

struct Player {
    int fd;
    std::string username;
    bool has_down_cards = false;
    bool has_up_cards = false;
    std::string up_card_symbol;
    int card_symbol = 0;

};

    sf::Text createText(const sf::Font& font, const std::string& str, int size, sf::Vector2f pos = sf::Vector2f(0,0), sf::Color color = sf::Color::White) {
        sf::Text text;
        text.setFont(font);
        text.setString(str);
        text.setCharacterSize(size);
        text.setFillColor(color);
        text.setPosition(pos);
        return text;
    };

struct InLobby {
    sf::RectangleShape StartButton;
    sf::Text GameInfo; 
    sf::Text Owner; 
    std::string owner_name;
    std::vector<std::string> usernames;
    
    int my_position;
    
    std::string my_name;

    std::vector<Player> players;

    std::string current_player_turn_name;

    bool totem_held_by_else = false;
    bool totem_held_by_me = false;

    bool totem_held;

    int totem_holder;
    int totem_holder_pos = -1;
    std::string totem_holder_name = "";

    std::string duel_winner = "";
    std::string duel_losers = "";

    bool there_was_duel = false;
                
    bool im_loser = false;
    bool many_loser = false;

    bool cards_in_the_middle = false;
    int middle_amount = 0;
    int up_amount = 0;
    int down_amount = 0;
    bool tried_to_catch_totem = false;


    int position_in_game;
    //TODO make it a mp so when you hive an fd you get plaer/

    // Can be done by posiotion
    //Turn card ise save here nad 

};

bool isClicked(const sf::RectangleShape& button, sf::Vector2i mousePos) {
    return button.getGlobalBounds().contains(sf::Vector2f(mousePos));
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
  //  int position_in_game;
    std::vector<sf::Texture> textures;
    sf::Clock clock;
    sf::Clock totemClock;
    sf::Clock duelClock;

    bool show_warrning = false;
    sf::Clock outwardClock;
    bool is_outward_card = false;
 
    sf::Clock gameStartedClock;
    bool can_trun_card = false;
    bool game_started= false;
    bool has_upside_cards= false;
    bool has_upward_cards= false;
    int shown_card_symbol= false;


    std::string game_winner_name = "";
    bool im_game_winner = false;

    bool failure_screen_bool = false;

    
    bool game_has_ended = false;

    std::string failure_msg;

    std::vector<char> msg_in{};

    int my_fd;


public:

    bool success_in_connect = false;    

    JungleSpeedClient(const std::string &ip, uint16_t port)
    {
        client_fd = socket(AF_INET, SOCK_STREAM, 0);
        in_game = false;
        if (client_fd == -1)
        {
            perror("Socket creation failed");
            failure_msg = "Socket creation failed";
            failure_screen_bool = true;
            //exit(EXIT_FAILURE);
            return;
        }
        success_in_connect = true;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

        setup_textures();

    }


    void setup_textures() {
        load_textures("img"); //0
        load_textures("outward_arrows"); //1
        load_textures("circle_inside_x_blue"); //2
        load_textures("circle_inside_x_red"); //3
        load_textures("circle_inside_x_yellow"); //4
        load_textures("circle_inside_x_green"); //5
        load_textures("circle_whole_x_blue"); //6
        load_textures("circle_whole_x_red"); //7
        load_textures("circle_whole_x_yellow"); //8
        load_textures("circle_whole_x_green"); //9
        load_textures("totem"); //10
        load_textures("crown"); //11
        

    }


    sf::Vector2f set_in_the_middle(sf::FloatRect bounds, int x_offset = 0, int y_offset = 0) {
        int y_pos = 300 - (bounds.height/2) + y_offset;
        int x_pos = 400 - (bounds.width/2) + x_offset;

        return sf::Vector2f(x_pos,y_pos);

    }

    sf::Vector2f offset_from_middle(int x_offset = 0, int y_offset = 0) {
        int y_pos = 300 + y_offset;
        int x_pos = 400 + x_offset;

        return sf::Vector2f(x_pos,y_pos);

    }

        

    sf::Vector2f get_center(const sf::Drawable &drawable) {
        if (const sf::RectangleShape* shape = dynamic_cast<const sf::RectangleShape*>(&drawable))
        {
            sf::FloatRect globalBounds = shape->getGlobalBounds();
            return sf::Vector2f(globalBounds.left + globalBounds.width / 2, globalBounds.top + globalBounds.height / 2);
        }
        else if (const sf::Sprite* sprite = dynamic_cast<const sf::Sprite*>(&drawable))
        {
            sf::FloatRect globalBounds = sprite->getGlobalBounds();
            return sf::Vector2f(globalBounds.left + globalBounds.width / 2, globalBounds.top + globalBounds.height / 2);
        }
        else if (const sf::Text* text = dynamic_cast<const sf::Text*>(&drawable))
        {
            sf::FloatRect globalBounds = text->getGlobalBounds();
            return sf::Vector2f(globalBounds.left + globalBounds.width / 2, globalBounds.top + globalBounds.height / 2);
        }
        else
        {
            return sf::Vector2f(0, 0);  
        }
    }

    sf::Vector2f center_in_button(const sf::Drawable &drawable, sf::FloatRect bounds) {
        sf::Vector2f v = get_center(drawable);
        int y_pos = v.y - (bounds.height/2);
        int x_pos = v.x - (bounds.width/2);
        return sf::Vector2f(x_pos,y_pos);
    };

    // TODO: Make it so flase stops program
    bool load_textures(std:: string name) {
        sf::Texture tex;
        sf::Image image;
        sf::Image scaledImage;

        int img_size = 80;
        scaledImage.create(img_size,img_size);
        std::string file_name = name + ".png";
        if(!image.loadFromFile(file_name)) {
            std::cerr << "Cannot load img" << std::endl;
            return false;
        }

        for(unsigned int x=0;x<static_cast<unsigned int>(img_size);++x) {
            for(unsigned int y=0;y<static_cast<unsigned int>(img_size);++y) {
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
            failure_screen_bool = true;
            in_game = false;
            perror("Connection failed");
            close(client_fd);
            failure_msg = "Connection failed";
            //exit(EXIT_FAILURE);
        }
    }

    void send_message(const std::string &message)
    {
        std::string final_message = message + msg_end_marker;
        send(client_fd, final_message.c_str(), final_message.size(), 0);
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

    std::string receive_message()
    {
        char buffer[BUFFER_SIZE] = {};
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0)
        {
            return "";
        }        
        std::string s(buffer);
        return s;
    }


    void set_card_texture(sf::Sprite &sprite, int position) {
        sprite.setTexture(textures[curr_lobby.players[position].card_symbol]);
    }

    void in_lobby_screen(sf::RenderWindow &window) {

        window.clear(sf::Color(30, 30, 30));


        sf::Event event;
        sf::Sprite spriteHidden;
        sf::Sprite spriteShown;
        

        // Start Game button
        sf::RectangleShape start_btn;

        //Showing a flashing message 
        start_warning_text(window);

        //Inform players about outward card;
        outward_card_text(window);
        
        //Infor players that game has started
        game_started_info_text(window);

        // Turn card button
        sf::RectangleShape turnCardBtn = turn_card_btn_and_text(window);

        // Start button
        start_btn = createBtn(sf::Vector2f(150, 60), sf::Color(169, 169, 169));
        start_btn.setPosition(set_in_the_middle(start_btn.getGlobalBounds()));
        if(curr_lobby.players.size() >1) {
            start_btn.setFillColor(sf::Color(100,200,100));
        }

        spriteHidden.setTexture(textures[0]);
        //spriteShown.setTexture(textures[0]);
        //set_card_texture(spriteShown);
        while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();

                // Wykrycie kliknięcia
                if (event.type == sf::Event::MouseButtonPressed) {
                            if (event.mouseButton.button == sf::Mouse::Left) {

                                if (isClicked(start_btn, sf::Mouse::getPosition(window)) && !game_started) {
                                    if(curr_lobby.players.size() < 2) {
                                        show_warrning = true;
                                        clock.restart();
                                    } else {
                                        start_game();
                                    }
                                }
                                if (isClicked(turnCardBtn, sf::Mouse::getPosition(window))) {
                                    send_turn_card();      
                                }
                                continue;
                    }
                } if ((event.type == sf::Event::KeyPressed) && game_started) {
                    if (event.key.code == sf::Keyboard::Space) {
                        curr_lobby.tried_to_catch_totem = true;
                        if(!curr_lobby.totem_held_by_else && !curr_lobby.totem_held_by_me) {
                            catch_totem_window();
                        }
                    }

                }
            } 

       // float player_gap = 50.f;  // Odstęp między graczami
       // float x_offset = 800 / 2; 
        int form_first = 0;
        int y_pos = 0;
        int x_pos = 0;
        int rotation = 0;
        int hidden_card_x = 0;
        int hidden_card_y = 0;
        int shown_card_x = 0;
        int shown_card_y = 0;
        //int card_offset = 45;

        // not_digonal_offset = 90;
        //int digonal_offset = 90/1.41;

        for(int i=curr_lobby.position_in_game; i>=0;i--) {




            get_card_position(i, x_pos, y_pos, rotation,
                     hidden_card_x, hidden_card_y,
                     shown_card_x, shown_card_y);

            sf::Text playerText = createText(font, curr_lobby.players[form_first].username, 
                18, sf::Vector2f(x_pos, y_pos), sf::Color::White);
            playerText.setRotation(rotation);

            //Setting cards on screen
            spriteHidden.setPosition(hidden_card_x,hidden_card_y);
            spriteHidden.setRotation(rotation);


            spriteShown.setPosition(shown_card_x,shown_card_y);
            spriteShown.setRotation(rotation);
            set_card_texture(spriteShown, form_first);

            window.draw(playerText);
            if(curr_lobby.players[form_first].has_up_cards) window.draw(spriteShown);
            if(curr_lobby.players[form_first].has_down_cards) window.draw(spriteHidden);

            form_first++;
            
        }

        int from_last = 0;
        for(std::vector<Player>::size_type i = curr_lobby.position_in_game + 1; i < curr_lobby.players.size(); ++i) {

            get_card_position(7-from_last, x_pos, y_pos, rotation,
                     hidden_card_x, hidden_card_y,
                     shown_card_x, shown_card_y);


            sf::Text playerText = createText(font, curr_lobby.players[i].username, 
                18, sf::Vector2f(x_pos, y_pos), sf::Color::White);
            playerText.setRotation(rotation);

            spriteHidden.setPosition(hidden_card_x,hidden_card_y);
            spriteHidden.setRotation(rotation);


            spriteShown.setPosition(shown_card_x,shown_card_y);
            spriteShown.setRotation(rotation);
            
            
            set_card_texture(spriteShown,i);
            
            window.draw(playerText);
            if(curr_lobby.players[i].has_up_cards) window.draw(spriteShown);
            if(curr_lobby.players[i].has_down_cards) window.draw(spriteHidden);
            from_last++;
        }




            if(!game_started) {
                window.draw(start_btn);
                start_btn_text(window, start_btn);
                //window.draw(startText);
                //window.draw(playerNuminfo);
                //window.draw(ownerMsg);
                info_about_players_text(window);
                owner_info_text(window);

            } else {
                std::string s = "Player's trun: " +  curr_lobby.current_player_turn_name;
                duel_info_text(window);
                cards_counters_text(window);
                sf::Text currTurnPlayer = createText(font, s, 10, sf::Vector2f(165, 290), sf::Color::White);
                
                draw_totem(window);
                window.draw(currTurnPlayer);


            }

        }
       

    //WSZSYTKIE FUNKCJIE DO WINDOWA

    //Ustawinia texotw

    sf::Text createText(const sf::Font& font, const std::string &str, int size, sf::Vector2f pos = sf::Vector2f(0,0), sf::Color color = sf::Color::White) {
        sf::Text text;
        text.setFont(font);
        text.setString(str);
        text.setCharacterSize(size);
        text.setFillColor(color);
        text.setPosition(pos);
        return text;
    };


    sf::RectangleShape createBtn(sf::Vector2f size, sf::Color color = sf::Color::White, sf::Vector2f pos = sf::Vector2f(0, 0)) {
        sf::RectangleShape btn;
        btn.setSize(size);
        btn.setFillColor(color);
        btn.setPosition(pos);
        return btn;
    }

    // Text na przycisku czy mozna zaczac
    void info_about_players_text(sf::RenderWindow &window) {
        sf::Text playerNuminfo;
        if(curr_lobby.players.size() < 2) {
            playerNuminfo = createText(font, WAIT_MSG, 
                12, sf::Vector2f(300, 330), sf::Color::White);
        } else {
            playerNuminfo = createText(font, ENOUGH_PLAYERS, 
                12, sf::Vector2f(300, 330), sf::Color(100, 200, 100));
        }
        window.draw(playerNuminfo);
    }


    void set_totem_position(sf::Sprite &totem) {
        if(curr_lobby.totem_holder_pos == -1) {
            totem.setPosition(set_in_the_middle(totem.getGlobalBounds()));
        } else if (curr_lobby.totem_holder_pos == 1) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), -300, 200));
        }else if (curr_lobby.totem_holder_pos == 2) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), -300, 0));
        }else if (curr_lobby.totem_holder_pos == 3) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), -300, -200));
        }else if (curr_lobby.totem_holder_pos == 4) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), 0, -200));
        }else if (curr_lobby.totem_holder_pos == 5) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), 300, -200));
        }else if (curr_lobby.totem_holder_pos == 6) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), 300, 0));
        }else if (curr_lobby.totem_holder_pos == 7) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), 300, 200));
        }else if (curr_lobby.totem_holder_pos == 0) {
             totem.setPosition(set_in_the_middle(totem.getGlobalBounds(), 0, 200));
        }
    }


    void set_holder_pos_and_name(int &fd) {
        int from_last = 0;
       // int form_first = 0;
        for(std::vector<Player>::size_type i = 0; i < curr_lobby.players.size(); ++i) {
             if(curr_lobby.players[i].fd == fd) {
                curr_lobby.totem_holder_pos = static_cast<std::vector<Player>::size_type>(curr_lobby.position_in_game) >= i ? 
                    curr_lobby.position_in_game - i : 7 - from_last;

                curr_lobby.totem_holder_name = curr_lobby.players[i].username;
                return;
            }
            if(static_cast<std::vector<Player>::size_type>(curr_lobby.position_in_game) < i) from_last++;
        }
    }

    // drawing totem
    void draw_totem(sf::RenderWindow &window) {
        sf::Sprite totem;
        sf::Text totem_text;
        sf::Text how_to_catch_text = createText(font, "To catch totem press spacebar", 10, sf::Vector2f(0,0), sf::Color(169,169,169));
        how_to_catch_text.setPosition(set_in_the_middle(how_to_catch_text.getGlobalBounds(),0,40));
        totem.setTexture(textures[10]);


        if(curr_lobby.totem_held_by_me && curr_lobby.tried_to_catch_totem && !(totemClock.getElapsedTime().asSeconds() < 0.4)) {
            totem_text = createText(font, "You have caught totem!",12,sf::Vector2f(300, 330), sf::Color(100, 200, 100));
        } else if (!curr_lobby.totem_held_by_me && curr_lobby.tried_to_catch_totem && !(totemClock.getElapsedTime().asSeconds() < 0.4)) {
            totem_text = createText(font, "Failed to catch totem!",12,sf::Vector2f(300, 330), sf::Color::Red);
        }
        totem_text.setPosition(offset_from_middle(100,6));
        window.draw(totem_text);
        window.draw(how_to_catch_text);
        totem_hloder_text(window);
        set_totem_position(totem);
        window.draw(totem);
    }


    void totem_hloder_text(sf::RenderWindow &window) {
        sf::Text totem_holder_text;
        if(curr_lobby.totem_held_by_me) {
            totem_holder_text = createText(font, "You are totem holder!", 12);
        } else if (curr_lobby.totem_held_by_else) {

            std::string totem_holder_stirng = "Totem holder: " + curr_lobby.totem_holder_name;
            totem_holder_text = createText(font, totem_holder_stirng, 12);
        }
        totem_holder_text.setPosition(offset_from_middle(100,-6));
        window.draw(totem_holder_text);
    }

    // Setting up trun card btn and text
    sf::RectangleShape turn_card_btn_and_text(sf::RenderWindow &window) {

        sf::RectangleShape btn;
        sf::Text text;
        if(game_started && can_trun_card) {
            btn = createBtn(sf::Vector2f(150, 60), sf::Color(100, 200, 100), sf::Vector2f(320,375));
            text = createText(font, "Pleas turn you card!", 
                12, sf::Vector2f(335, 405), sf::Color::White);
        } else if(game_started && !can_trun_card) {
            btn = createBtn(sf::Vector2f(150, 60),sf::Color(169, 169, 169), sf::Vector2f(320,375));
            text = createText(font, "Wait for your turn", 
                12, sf::Vector2f(335, 405), sf::Color::White);
        } 

        text.setPosition(center_in_button(btn, text.getGlobalBounds()));
        window.draw(btn);
        window.draw(text);
        return btn;
    }


        void start_btn_text(sf::RenderWindow &window,sf::RectangleShape &btn ){

            sf::Text text = createText(font, "Start", 18,
                                        sf::Vector2f(100,
                                                    100), sf::Color::White);
            auto [x,y] = center_in_button(btn, text.getGlobalBounds());  
            text.setPosition(x,y);
            window.draw(text);
        }
        

    // Funtcions using clock
    //Flashin warning after start game is clicked 
    void start_warning_text(sf::RenderWindow &window) {
        sf::Text warning; 
        if(show_warrning && clock.getElapsedTime().asSeconds() < 2 && curr_lobby.owner_name == username) {
            warning = createText(font, "Not enough players, cant start game", 
                18, sf::Vector2f(200, 200), sf::Color::Red);
            window.draw(warning);
        } else if (show_warrning && clock.getElapsedTime().asSeconds() < 2 && curr_lobby.owner_name != username) {
            warning = createText(font, "You are not an owner, you cant start game", 
                18, sf::Vector2f(200, 200), sf::Color::Red);
        } else {
            show_warrning = false;
            return;
        }
        window.draw(warning);
    }

    void outward_card_text(sf::RenderWindow &window) {
        // Inform about outward card
        if(is_outward_card && outwardClock.getElapsedTime().asSeconds() < 1) {

            sf::Text outwardMsg = createText(font, "Outward card has appeard! All cards have been turend up!", 
                18, sf::Vector2f(200, 200), sf::Color::Red);
            
            window.draw(outwardMsg);
        } else {
            is_outward_card = false;
        }
    }


    //Flashing msg that game has started
    void game_started_info_text(sf::RenderWindow &window) {
        if(game_started && gameStartedClock.getElapsedTime().asSeconds() < 2) {
            sf::Text gameStarted =  createText(font, "Game Started! Good luck!", 
                18, sf::Vector2f(215, 115), sf::Color(100, 200, 100));

            gameStarted.setPosition(set_in_the_middle(gameStarted.getGlobalBounds(), 0, -130));

            window.draw(gameStarted);
        };
    }



    //Funtcion to draw duel info

    void duel_info_text(sf::RenderWindow &window) {
        sf::Text textUp;
        sf::Text textDown;
        
        int up_x = -145;
        int down_x= -120;

       if(duelClock.getElapsedTime().asSeconds() < 4) {
            if(curr_lobby.duel_winner == curr_lobby.my_name) {
                textUp = createText(font, "You are the winner!",24);
                textUp.setFillColor(sf::Color(100,200,100));
                textUp.setPosition(set_in_the_middle(textUp.getGlobalBounds(), 0, up_x));

                std::string losers;
                if(curr_lobby.many_loser) {
                    losers = "The losers are:" + curr_lobby.duel_losers;
                } else {
                    losers = "Loser is" + curr_lobby.duel_losers;
                }
                textDown = createText(font, losers,24);
                textDown.setPosition(set_in_the_middle(textDown.getGlobalBounds(), 0, down_x));


            } else if(curr_lobby.im_loser) {
                textUp = createText(font, "You have lost the duel!",24);
                textUp.setFillColor(sf::Color::Red);
                textUp.setPosition(set_in_the_middle(textUp.getGlobalBounds(), 0, up_x));

                std::string winner = "The winner is " + curr_lobby.duel_winner;

                textDown = createText(font, winner,24);
                textDown.setPosition(set_in_the_middle(textDown.getGlobalBounds(), 0, down_x));

            } else {
                std::string winner = "The winner is " + curr_lobby.duel_winner;
                textUp = createText(font, winner,24);
                textUp.setPosition(set_in_the_middle(textUp.getGlobalBounds(), 0, up_x));

                std::string losers;
                if(curr_lobby.many_loser) {
                    losers = "The losers are:" + curr_lobby.duel_losers;
                } else {
                    losers = "Loser is" + curr_lobby.duel_losers;
                }

                textDown = createText(font, losers,24);
                textDown.setPosition(set_in_the_middle(textDown.getGlobalBounds(), 0, down_x));
            }
        
            window.draw(textUp);
            window.draw(textDown);

       } 
    }

    // Info about who is owner
    void owner_info_text(sf::RenderWindow &window) {
        sf::Text ownerMsg;
        if(curr_lobby.owner_name == username) {
            ownerMsg = createText(font, ONWER_YOU, 
                12, sf::Vector2f(330, 348), sf::Color(169, 169, 169));
        } else {
            std::string info_msg = ONWER_ELSE + curr_lobby.owner_name;
                        ownerMsg = createText(font, info_msg, 
                12, sf::Vector2f(330, 348), sf::Color(169, 169, 169));
        }
        window.draw(ownerMsg);
    }

    void cards_counters_text(sf::RenderWindow &window) {
        sf::Text upCount;
        sf::Text downCount;
        sf::Text middleCount;
        std::string s;
        int x_pos = 220;

        // To jakos idzie zrefactorowac
        s = "Cards up: " + std::to_string(curr_lobby.up_amount);
        cards_counters_text_creator(window, upCount, s, x_pos);
        x_pos = x_pos + s.size()*8 + 10;

        s = "Card down: " +  std::to_string(curr_lobby.down_amount);
        cards_counters_text_creator(window, downCount, s, x_pos);
        x_pos = x_pos + s.size()*8 + 10;

        s = "Card middle: " +  std::to_string(curr_lobby.middle_amount);
        cards_counters_text_creator(window, middleCount, s, x_pos);
    }

    // stowrzenie tektos
    void cards_counters_text_creator(sf::RenderWindow &window, sf::Text &text, std::string &s, int &x_pos) {
        text.setFont(font);
        text.setCharacterSize(8);
        text.setFillColor(sf::Color(sf::Color::White)); // Grey color
        text.setPosition(x_pos, 355);
        text.setString(s);
        window.draw(text);
    }


    void chose_screen(sf::RenderWindow &window) {
        sf::Event event;

        // Join button
        sf::RectangleShape join_btn;
        sf::Text StartText; 
        if(lobbyList.size()<MAX_GAMES_COUNT) {
        join_btn.setSize(sf::Vector2f(100, 40));
        join_btn.setFillColor(sf::Color(100, 200, 100));
        join_btn.setPosition(350, 550);
        StartText = createText(font, "Create", 16,
                                    sf::Vector2f(380,
                                                560));
        StartText.setPosition(center_in_button(join_btn,StartText.getGlobalBounds()));
        } else {
        StartText = createText(font, "Maximum number of lobbies", 16,
                                    sf::Vector2f(380,
                                                560));
        StartText.setPosition(set_in_the_middle(StartText.getGlobalBounds(), 0, 280));
        }
        
        while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();

                // Wykrycie kliknięcia
                if (event.type == sf::Event::MouseButtonPressed) {
                    if (event.mouseButton.button == sf::Mouse::Left) {
                        if (isClicked(join_btn, sf::Mouse::getPosition(window))) {
                                create_game();
                        }
                        for (auto& lobby : lobbyList) {
                            if (isClicked(lobby.button, sf::Mouse::getPosition(window))) {
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
                sf::Text joinText = createText(font, "Join", 20,
                                            sf::Vector2f(lobby.button.getPosition().x + 55,
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
            } else if(failure_screen_bool) {
                failure_screen(window);
            } else if(game_has_ended) {
                winner_screen(window);
            }
            else {
                chose_screen(window);
            }
            window.display();
         }            
         std::exit(0);
    }

    void run()
    {
        connect_to_server();
        get_username();

        std::thread receiver([this]() {
            handle_response();
        });        
        list_games();
        receiver.detach();

        while (true) {};         
        close(client_fd);
    }


        void failure_screen(sf::RenderWindow &window) {
            sf::Event event;
            while (window.pollEvent(event)) {
                // Obsługa zdarzeń
                if (event.type == sf::Event::Closed) {
                    window.close();
                }
            }
            window.clear(sf::Color(30, 30, 30));
            sf::Text info = createText(font, failure_msg, 32);
            info.setPosition(set_in_the_middle(info.getGlobalBounds()));
            window.draw(info);
        }

    std::vector<json> parse_msg (std::string &msg){ 
        
        std::vector<json> result;

        if(msg.find("}{") != std::string::npos) {
            std::istringstream stream(msg);

            std::string temp_msg;
            std::string root; 

            temp_msg = msg;

            size_t start = 0;
            size_t end;

            while ((end = temp_msg.find("}{", start)) != std::string::npos)  {
                root = temp_msg.substr(start, end - start + 1);
                temp_msg = temp_msg.substr(end - start + 1, temp_msg.size());
                result.push_back(json::parse(root));
            }
            root = temp_msg;
            result.push_back(json::parse(root));
            return result;
        } else {
            result.push_back(json::parse(msg));
            return result;
        }

    }


    void handle_response() {
        while(true) {
        std::string response = receive_message();
        std::vector<json> roots;

        if (!response.empty())
        {   
            
            roots = parse_msg(response);
        }
        else
        {
            in_game = false;
            failure_screen_bool = true;
            failure_msg = "No connection with server.";
            return;
        }

        for(const json &r : roots) {
            json root = r;
        std::string action = root["response"].get<std::string>();

        if(action == "UPDATE_LOBBIES") {
                update_lobbies(root);
            } else if (action == "CREATE_GAME") {
                create_game_respone(root);
            }
            else if (action == "JOIN_GAME") {
                join_game_response(root);
            } else if (action == "IN_LOBBY_UPDATE") {
                in_lobby_update(root);
            } else if (action == "START") {
                game_started = true;
                gameStartedClock.restart();
            } else if(action == "CAN_TURN_CARD") {
                set_can_trun_card(true);
            } else if(action == "TURNED_CARD") {
                turn_card_response(root);
            } else if(action == "CARDS_COUNTS"){
                cards_count_response(root);

            } else if(action == "NEXT_TURN") {
                next_turn_response(root);
            } else if(action == "TOTEM") {
                totem_response(root);
            } else if(action == "OUTWARDS_ARROWS_TURNED_CARDS") {
                is_outward_card = true;
                parse_players_by_fd_outward(root);
                outwardClock.restart();
            } else if(action == "DUEL") {
                duel_respones(root);
            } else if(action == "CATCH_TOTEM") {
                catch_totem_response(root);
            } else if(action == "END") {
               end_game_response(root);        
            }
        }
        }
    }

    void end_game_response(json &root) {
        int winner_fd = root["Winner"].get<int>();
        game_winner_name = get_player_name(winner_fd);
        if(game_winner_name == username) im_game_winner = true;
        game_has_ended = true;
        in_game = false;
    }

    void winner_screen(sf::RenderWindow &window) {
        sf::Text textUp;
        sf::Text textDown;
        sf::Event event;
        sf::Sprite sprite;
        window.clear(sf::Color(30, 30, 30));
        sprite.setTexture(textures[11]);

        while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();
            } 

        if(im_game_winner) {
            textUp = createText(font, "You have won the game!!", 32);
            textUp.setFillColor(sf::Color(100,200,100));
            textUp.setPosition(set_in_the_middle(textUp.getGlobalBounds(),0, -180));

            textDown = createText(font, "Congratulations!", 24);
            textUp.setFillColor(sf::Color(100,200,100));
            textDown.setPosition(set_in_the_middle(textDown.getGlobalBounds(),0, -150));

            sprite.setPosition(set_in_the_middle(sprite.getGlobalBounds(), 0 , -230));
        } else {


            textUp = createText(font, game_winner_name, 32);
            textUp.setFillColor(sf::Color(100,200,100));
            textUp.setPosition(set_in_the_middle(textUp.getGlobalBounds(),0, -180));


            textDown = createText(font, "Is the game winner! Congratulations!!", 24);
            textUp.setFillColor(sf::Color(100,200,100));
            textDown.setPosition(set_in_the_middle(textDown.getGlobalBounds(),0,  -150));
            sprite.setPosition(set_in_the_middle(sprite.getGlobalBounds(), 0 , -230));
        }

        window.draw(sprite);
        window.draw(textUp);
        window.draw(textDown);

    }

private:

    void catch_totem_response(json &root) {

        if(root["success"].get<bool>() == true) {
            curr_lobby.totem_held_by_me = true;
            curr_lobby.totem_holder_pos = 0;
            totemClock.restart();
        } else if(curr_lobby.tried_to_catch_totem == true && root["success"].get<bool>() == false) {
            curr_lobby.totem_held_by_me = false;
            totemClock.restart();
            //curr_lobby.tried_to_catch_totem = false;
        }
        
    }

    void duel_respones(json &root) {
        clear_duel();
        
        curr_lobby.duel_losers = "";
        int winner = root["winner"].get<int>();
        curr_lobby.duel_winner = get_player_name(winner);
        std::vector<int> losers = root["losers"].get<std::vector<int>>();
        if(losers.size()>1) curr_lobby.many_loser = true;

        curr_lobby.there_was_duel = true;

        for(auto &i : losers) {
            std::string loser_name = get_player_name(i);
            curr_lobby.duel_losers = curr_lobby.duel_losers + " " + loser_name;
            if(loser_name == username) {
                curr_lobby.im_loser = true;
            };
        };
        duelClock.restart();

    };

    std::string get_player_name(int &fd) {
        for(auto &player : curr_lobby.players) {
            if(player.fd == fd) {
                return player.username;
            }
        }
        return "";
    }

    void outwards_response() {

    }

    // Funkcja do przetwarzania playerow przez fd
    //Jedna jest od ustawienia up and down bool values zeby pokazywac karty lub nie
    // Druga to update karty playera

    void parse_players_by_fd_outward(json &root) {
        for(auto &p : curr_lobby.players) {
            p.up_card_symbol = root[std::to_string(p.fd)].get<std::string>();
            find_new_card(p.up_card_symbol, p);
            p.has_up_cards = true;
        }
    }

    void parse_players_by_fd_count(json &root) {

        std::string up,down;
        for(auto &p : curr_lobby.players) {

            std::istringstream iss(root[std::to_string(p.fd)].get<std::string>());
            iss >> up >> down;
            
            p.has_up_cards = (up == "true") ? true : false;
            p.has_down_cards = (down == "true") ? true : false;
        }

    }

    void set_can_trun_card(bool status) {
        can_trun_card = status;
    }

    void send_turn_card() {
        json request;
        request["action"] = "TURN_CARD";
        send_message(request.dump());
    }

    void totem_response(json &root) {
        curr_lobby.totem_held_by_else = root["held"].get<bool>();
        if(curr_lobby.totem_held_by_else) {
            curr_lobby.totem_held_by_me = false;
            curr_lobby.totem_held_by_else = true;
            curr_lobby.totem_holder = root["by"].get<int>();
            set_holder_pos_and_name(curr_lobby.totem_holder);
        } else {
            clear_totem_holder();
        };
    };


    void clear_totem_holder() {
        curr_lobby.totem_held_by_me = false;
        curr_lobby.totem_held_by_else = false;
        curr_lobby.tried_to_catch_totem = false;
        curr_lobby.totem_holder_name = "";
        curr_lobby.totem_holder_pos = -1;
    }

    void clear_duel() {
        curr_lobby.there_was_duel = false;
        curr_lobby.duel_losers = false;
        curr_lobby.duel_winner = false;
        curr_lobby.im_loser = false;
    }

    void next_turn_response(json &root) {
        curr_lobby.current_player_turn_name = root["next_player"].get<std::string>();
        if(curr_lobby.current_player_turn_name == curr_lobby.my_name) {
            can_trun_card = true;
        } else {
            can_trun_card = false;
        }

    }

    void turn_card_response(json &root) {
        int player_fd = root["by"].get<int>();
        std::string card = root["card"].get<std::string>();
        can_trun_card = false;

        for(auto &player : curr_lobby.players) { 
            if(player.fd == player_fd) {
                player.up_card_symbol = card;
                find_new_card(card, player);
                break;
            }
        }
    }

    void find_new_card(std::string card, Player &player) {

        if (card == "outward_arrows") {
            player.card_symbol = 1;
        } else if(card == "circle_inside_x_blue") {
            player.card_symbol = 2;
        } else if(card == "circle_inside_x_red") {
            player.card_symbol = 3;
        } else if(card == "circle_inside_x_yellow") {
            player.card_symbol = 4;
        } else if(card == "circle_inside_x_green") {
            player.card_symbol = 5;
        } else if(card == "circle_whole_x_blue") {
            player.card_symbol = 6;
        } else if(card == "circle_whole_x_red") {
            player.card_symbol = 7;
        } else if(card == "circle_whole_x_yellow") {
            player.card_symbol = 8;
        } else if(card == "circle_whole_x_green") {
            player.card_symbol = 9;
        } else {
            std::cerr << "Nieznana karta: " << card << std::endl;
        }
    }


    void cards_count_response(json &root) {

        parse_players_by_fd_count(root);
        curr_lobby.up_amount = root["up"].get<int>();
        curr_lobby.down_amount = root["down"].get<int>();
        curr_lobby.middle_amount = root["middle"].get<int>();
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
                if(!game["is_started"].get<bool>()) temp_lobby_list.push_back(createLobby(game["game_id"].get<int>(), game["player_count"].get<int>(), font, sf::Vector2f(50, 20 + temp_lobby_list.size() * 70)));
        }
        lobbyList = std::move(temp_lobby_list);
    }


    void set_my_position() {
        for(int i = 0; curr_lobby.players.size(); i++) {
            if(curr_lobby.players[i].fd == client_fd) {
                curr_lobby.position_in_game = i;
                return;
            }
        }
    };

    void create_game_respone(json &root) {
        if (root["success"].get<bool>())
        {
            setup_usernames(root);
            setup_players(root);
            curr_lobby.position_in_game = root["position"].get<int>();
            set_is_owner(true);
            set_in_game(true);
            curr_lobby.owner_name = root["owner"].get<std::string>();
        }
        else
        {
            set_in_game(false);
            set_is_owner(false);
        }
    }

    void in_lobby_update(json &root) {
        setup_usernames(root);
        setup_players(root);
        setup_owner(root["owner"].get<std::string>());

    }

    //void 

    void setup_usernames(json &root) {
        curr_lobby.usernames = split_msg(root["usernames"].get<std::string>());
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

    void join_game_click(int id)
    {
        json request;
        request["action"] = "JOIN_GAME";
        request["game_id"] = id;
        send_message(request.dump());
    }

    void create_players_list(std::vector<std::string> &u, std::vector<std::string> &f) {
        
        std::vector<Player> temp_players;

        for(std::vector<std::string>::size_type i = 0; i < u.size(); ++i) {
            Player player;
            int fd = std::stoi(f[i]);
            player.fd = fd;
            player.username = u[i];

            temp_players.push_back(player);
        }

        curr_lobby.players = temp_players;
    };

    void setup_players(json &root) {
        std::vector<std::string> temp_usernames = split_msg(root["usernames"].get<std::string>());
        std::vector<std::string> temp_fds = split_msg(root["fds"].get<std::string>());
        create_players_list(temp_usernames, temp_fds);
    }

    void join_game_response(json &root) {

        if(root["success"].get<bool>()) {        
        setup_players(root);
        curr_lobby.usernames = split_msg(root["usernames"].get<std::string>());
        curr_lobby.position_in_game = root["position"].get<int>();
        curr_lobby.owner_name = root["owner"].get<std::string>();
        set_in_game(true);
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
            /*
            if (root.contains("error"))
            {
                std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "Turned card: " << root["card"].get<std::string>() << "\n";
            }
            */
        }
        else
        {
            in_game = false;
            failure_screen_bool = true;
            failure_msg = "No connection with server.";
        }
    }

      void catch_totem_window() {
        auto timestamp = get_current_timestamp();
        json request;
        request["action"] = "CATCH_TOTEM";
        request["timestamp"] = timestamp;
        send_message(request.dump());
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
            /*
            if (root.contains("error"))
            {
                std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                std::cout << "Totem caught!\n";
                
            }
            */
        }
        else
        {
            in_game = false;
            failure_screen_bool = true;
            failure_msg = "No connection with server.";
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

    std::vector<std::string> split_msg(std::string usernames) {
        std::vector<std::string> words; 
        std::istringstream stream(usernames);
        std::string word; 

        // Dzielimy string na wyrazy, używając strumienia
        while (stream >> word) {
            words.push_back(word); // Dodajemy każdy wyraz do wektora
        }

        return words;
    }

    void setup_owner(std::string name) {
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
                //std::cout << "Error: " << root["error"].get<std::string>() << "\n";
            }
            else
            {
                username = root["username"].get<std::string>();
                curr_lobby.my_name = username;
            }
        }
        else
        {
            in_game = false;
            failure_screen_bool = true;
            failure_msg = "No connection with server.";
        }
    }


    void get_card_position(int i, int &x_pos, int &y_pos, int &rotation,
                     int &hidden_card_x, int &hidden_card_y,
                     int &shown_card_x, int &shown_card_y) {
        if(i== 7) {
            x_pos = 700;
            y_pos = 540;
            rotation = -45;

            shown_card_y = 530;
            shown_card_x = 590;


            hidden_card_y = shown_card_y - 63;
            hidden_card_x = shown_card_x + 63;

        } else if(i==6) {
            x_pos = 750;
            y_pos = 350;
            rotation = -90;


            hidden_card_y = 295;
            hidden_card_x = 670;
            shown_card_y = 380;
            shown_card_x = 670;


        /*

            hidden_card_y = 205;
            hidden_card_x = 130;
            shown_card_y = 295;
            shown_card_x = 130;
            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;
            */
        } else if(i==5) {
            x_pos = 750;
            y_pos = 100;
            rotation = -135;


            hidden_card_y = 137;
            hidden_card_x = 647;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x + 63;


/*
            shown_card_y = 137;
            shown_card_x = 647;
            hidden_card_y = shown_card_y + 63;
            hidden_card_x = shown_card_x - 63;

            */
        } else if (i==4){
            x_pos = 400;
            y_pos = 50;
            rotation = -180;
// To change

            hidden_card_y = 70;
            hidden_card_x = 210;
            shown_card_y = hidden_card_y + 63;
            shown_card_x = hidden_card_x - 63;

            hidden_card_y = 140;
            hidden_card_x = 390;
            shown_card_y = 140;
            shown_card_x = 475;

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

            hidden_card_y = 460;
            hidden_card_x = 300;
            shown_card_y = 460;
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
    if(client.success_in_connect) client.run();
    return 0;
}

