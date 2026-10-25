#pragma once

#include "engine/core/Math.h"

#include <cstdint>
#include <vector>

namespace eng {
class Renderer2D;
}

namespace ww {

// Prozedural gezeichnete Kulisse: Himmel, Sterne, Mond, Wald, Dorf und Lagerfeuer.
// `night` läuft von 0 (Tag) bis 1 (Nacht) und blendet alle Farben weich über.
class Scenery {
public:
    void init(uint64_t seed);
    void drawSky(eng::Renderer2D& r, float night, double time) const;
    void drawHorizon(eng::Renderer2D& r, float night, double time, float horizonY) const;
    void drawCampfire(eng::Renderer2D& r, eng::Vec2 pos, float scale, double time, float intensity) const;
    // Dunkler Rand, der den Blick zur Mitte lenkt
    void drawVignette(eng::Renderer2D& r, float strength) const;

private:
    struct Star {
        float x, y, size, phase, speed;
    };
    struct Tree {
        float x, height, width;
        int layer;
    };
    struct House {
        float x, width, height, roof;
        bool lit;
    };
    struct Cloud {
        float x, y, size, speed;
    };
    std::vector<Star> m_stars;
    std::vector<Tree> m_trees;
    std::vector<House> m_houses;
    std::vector<Cloud> m_clouds;
};

} // namespace ww
