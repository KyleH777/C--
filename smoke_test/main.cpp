#include <SFML/Graphics.hpp>

int main()
{
    // Create a 800x600 window titled "SFML Smoke Test"
    sf::RenderWindow window(sf::VideoMode(800, 600), "SFML Smoke Test");

    // Event loop — keeps the window alive until the user closes it
    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            // The only exit: clicking the X / pressing Alt+F4
            if (event.type == sf::Event::Closed)
                window.close();
        }

        window.clear(sf::Color::Black); // fill with black
        window.display();               // show the frame
    }

    return 0;
}
