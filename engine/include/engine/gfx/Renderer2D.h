#pragma once

#include "engine/core/Math.h"
#include "engine/core/Math3D.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eng {

class Font;

enum class BlendMode { Alpha, Additive };

// Affine 2D-Abbildung: x' = a*x + c*y + tx, y' = b*x + d*y + ty
struct Affine2 {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Affine2 operator*(const Affine2& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.tx + c * o.ty + tx,
                b * o.tx + d * o.ty + ty};
    }
    // Drehung (Bogenmaß) und Skalierung um einen Punkt
    static Affine2 around(Vec2 pivot, float angle, float scale = 1.0f);
};

// Gebündelter 2D-Renderer. Formen (abgerundete Rechtecke, Kreise, Linien) werden
// per Signed-Distance-Function im Shader kantengeglättet gezeichnet.
// Koordinaten sind logische Pixel, Ursprung oben links.
class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    bool init();
    void shutdown();

    void beginFrame(int framebufferWidth, int framebufferHeight, float dpiScale);
    void endFrame();
    void clear(Color c);

    float width() const { return m_width; }
    float height() const { return m_height; }
    float dpiScale() const { return m_scale; }
    int drawCalls() const { return m_drawCalls; }

    void rect(const Rect& r, Color c);
    void gradientV(const Rect& r, Color top, Color bottom);
    void gradientH(const Rect& r, Color left, Color right);
    void roundRect(const Rect& r, float radius, Color c);
    void roundRectOutline(const Rect& r, float radius, float thickness, Color c);
    void circle(Vec2 center, float radius, Color c);
    void circleOutline(Vec2 center, float radius, float thickness, Color c);
    // Weicher, radialer Lichtschein (z. B. Feuer, Mond)
    void glow(Vec2 center, float radius, Color c);
    void line(Vec2 a, Vec2 b, float thickness, Color c);
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color);

    // Text, `pos` ist die linke obere Ecke der Zeile
    void text(Font& font, Vec2 pos, std::string_view s, Color c);
    // Text mit expliziter Grundlinie
    void textBaseline(Font& font, Vec2 pos, std::string_view s, Color c);
    // Zentriert Text horizontal und vertikal (anhand der Versalhöhe) im Rechteck
    void textCentered(Font& font, const Rect& r, std::string_view s, Color c);
    void textRight(Font& font, Vec2 topRight, std::string_view s, Color c);
    // Text mit zusätzlichem Zeichenabstand (Sperrsatz für Überschriften)
    void textSpaced(Font& font, Vec2 pos, std::string_view s, float spacing, Color c);
    static float measureSpaced(Font& font, std::string_view s, float spacing);

    // Bild aus einer Textur (RGBA). uv außerhalb von 0..1 wiederholt die Textur, falls sie so erstellt wurde.
    void image(unsigned texture, const Rect& r, Vec2 uv0, Vec2 uv1, Color tint);
    static unsigned createTexture(int width, int height, const uint8_t* rgba, bool repeat);
    static void destroyTexture(unsigned texture);

    void setBlendMode(BlendMode mode);
    // Alles danach Gezeichnete wird zusätzlich so abgebildet (z. B. schräg angenagelte Zettel)
    void pushTransform(const Affine2& t);
    void popTransform();
    // Texturiertes Dreieck (für unregelmäßige Formen wie ausgefranstes Papier)
    void texturedTriangle(unsigned texture, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 uv0, Vec2 uv1, Vec2 uv2, Color tint);
    // Beleuchtetes Material (Farbe in RGB, Höhe im Alphakanal) – wirft Relief im Licht von setLight
    void litTriangle(unsigned texture, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 uv0, Vec2 uv1, Vec2 uv2, Color tint, float relief);
    void litImage(unsigned texture, const Rect& r, Vec2 uv0, Vec2 uv1, Color tint, float relief);
    // Lichtquelle für beleuchtete Materialien (logische Pixel; height = Abstand über der Fläche)
    void setLight(Vec2 position, float height, Vec3 color, Vec3 ambient, float range);
    // Weicher Schatten (gilt auch in gedrehten Bereichen)
    void softRect(const Rect& r, float radius, float softness, Color c);
    // Einfarbiges Dreieck mit Farbe je Ecke
    void triangleColors(Vec2 a, Vec2 b, Vec2 c, Color ca, Color cb, Color cc);
    void pushClip(const Rect& r);
    void popClip();

    // Liest das fertige Bild (RGBA, Zeilen von oben nach unten). Nach endFrame() aufrufen.
    std::vector<uint8_t> readPixels() const;
    int framebufferWidth() const { return m_fbWidth; }
    int framebufferHeight() const { return m_fbHeight; }

private:
    struct Vertex {
        float x, y;
        float u, v;
        uint32_t color;
        float lx, ly;   // lokale Koordinate relativ zum Formmittelpunkt
        float hx, hy;   // halbe Formgröße
        float radius;   // Eckenradius
        float border;   // Randstärke (0 = gefüllt)
        float mode;     // 0 Textur, 1 SDF-Form, 2 Lichtschein, 3 Vollfarbe
    };

    void shape(const Rect& r, float radius, float border, float mode, Color top, Color bottom);
    void push(const Vertex& v);
    void setTexture(unsigned texture);
    void flush();
    void applyClip();

    unsigned m_program = 0;
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_whiteTexture = 0;
    int m_projLocation = -1;
    int m_lightPosLoc = -1;
    int m_lightHeightLoc = -1;
    int m_lightColorLoc = -1;
    int m_ambientLoc = -1;
    int m_lightRangeLoc = -1;
    unsigned m_currentTexture = 0;
    BlendMode m_blend = BlendMode::Alpha;
    std::vector<Vertex> m_vertices;
    std::vector<Rect> m_clipStack;
    std::vector<Affine2> m_transforms;
    int m_fbWidth = 1;
    int m_fbHeight = 1;
    float m_scale = 1.0f;
    float m_width = 1.0f;
    float m_height = 1.0f;
    int m_drawCalls = 0;
};

} // namespace eng
