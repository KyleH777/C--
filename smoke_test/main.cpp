#include <SFML/Graphics.hpp>
#include <cmath>
#include <string>

// Build a regular N-sided polygon centred at (0,0) with the given radius.
sf::ConvexShape makePolygon(int sides, float radius)
{
    sf::ConvexShape shape(sides);
    for (int i = 0; i < sides; ++i)
    {
        float angle = i * (2.f * 3.14159265f / sides) - 3.14159265f / 2.f;
        shape.setPoint(i, { radius * std::cos(angle), radius * std::sin(angle) });
    }
    shape.setOrigin(0.f, 0.f);
    return shape;
}

// Map a hue in [0, 360) to an RGB colour (full saturation + value).
sf::Color hsvToRgb(float hue)
{
    float h = hue / 60.f;
    int   i = static_cast<int>(h);
    float f = h - i;
    float q = 1.f - f;

    auto b = [](float v){ return static_cast<sf::Uint8>(v * 255.f); };

    switch (i % 6)
    {
        case 0: return { b(1), b(f), b(0) };
        case 1: return { b(q), b(1), b(0) };
        case 2: return { b(0), b(1), b(f) };
        case 3: return { b(0), b(q), b(1) };
        case 4: return { b(f), b(0), b(1) };
        default: return { b(1), b(0), b(q) };
    }
}

int main()
{
    sf::RenderWindow window(
        sf::VideoMode(800, 600),
        "SFML Smoke Test  |  C++ is alive!",
        sf::Style::Close
    );
    window.setFramerateLimit(60);

    const sf::Vector2f centre(400.f, 300.f);

    // Three concentric polygons: hexagon > triangle > square
    sf::ConvexShape hex  = makePolygon(6, 180.f);
    sf::ConvexShape tri  = makePolygon(3, 110.f);
    sf::ConvexShape quad = makePolygon(4,  60.f);

    for (auto* s : { &hex, &tri, &quad })
    {
        s->setPosition(centre);
        s->setFillColor(sf::Color::Transparent);
        s->setOutlineThickness(3.f);
    }

    sf::Clock clock;
    sf::Clock fpsClock;
    int       frameCount = 0;

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
            if (event.type == sf::Event::Closed ||
               (event.type == sf::Event::KeyPressed &&
                event.key.code == sf::Keyboard::Escape))
                window.close();

        const float t   = clock.getElapsedTime().asSeconds();
        const float hue = std::fmod(t * 60.f, 360.f);   // full cycle every 6 s

        // Each shape rotates at a different speed and direction
        hex .setRotation( t * 20.f);
        tri .setRotation(-t * 35.f);
        quad.setRotation( t * 55.f);

        // Colours are offset around the hue wheel so they're always different
        hex .setOutlineColor(hsvToRgb(hue));
        tri .setOutlineColor(hsvToRgb(std::fmod(hue + 120.f, 360.f)));
        quad.setOutlineColor(hsvToRgb(std::fmod(hue + 240.f, 360.f)));

        // FPS counter — update title every 30 frames
        ++frameCount;
        if (frameCount == 30)
        {
            float fps = 30.f / fpsClock.restart().asSeconds();
            window.setTitle("SFML Smoke Test  |  C++ is alive!  |  "
                            + std::to_string(static_cast<int>(fps)) + " FPS");
            frameCount = 0;
        }

        window.clear(sf::Color(15, 15, 20));  // near-black background
        window.draw(hex);
        window.draw(tri);
        window.draw(quad);
        window.display();
    }
}
