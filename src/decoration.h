#pragma once

#include <QObject>
#include <QColor>
#include <QList>
#include <QHash>
#include <QString>
#include <optional>
#include "wlroots.h"

// Forward deps
class Toplevel;
class Animation;
class AnimationManager;
class Config;

// ---------- Decoration data types ----------

struct GradientConfig {
    bool enabled = false;
    QList<QColor> colors; // 2+ colors, fallback to solid if <2
    double angle = 0; // degrees, 0=left->right, 90=top->bottom
    bool animate = false; // cycle gradient slowly

    QColor sample(double t) const; // t 0..1 linear interpolation through colors
};

struct BorderConfig {
    bool enabled = true;
    int width = 2;        // px logical
    int radius = 8;       // px corner radius
    QColor activeColor = QColor("#ff5500");
    QColor inactiveColor = QColor("#3a3a3a");
    GradientConfig gradient; // if enabled, overrides solid colors (or blends)

    // animation for focus switch / border highlight
    bool animate = true;
    int animationDuration = 200; // ms
    QString animationEasing = "easeOutCubic";
};

struct TitleBarConfig {
    bool enabled = false; // user can remove title bars entirely
    int height = 28;
    QColor color = QColor("#222222");
    QColor textColor = QColor("#eeeeee");
    int fontSize = 11;
    bool showTitle = true;
    bool showButtons = true;
};

struct DecorationConfig {
    BorderConfig border;
    TitleBarConfig titlebar;
    // gaps / padding inside usable area? handled by layout but we keep here
    int outerGap = 0;
    int innerGap = 0;
};

// ---------- Runtime decoration object ----------

/*
 * WindowDecoration manages per-Toplevel scene nodes for borders/titlebar and radius.
 * It owns wlr_scene_rect nodes (border edges) and optionally a titlebar tree.
 * Animations for border color are driven via AnimationManager: the decoration creates/uses
 * a shared Animation per window (id like "border:focus:<ptr>") that interpolates t 0..1.
 */

class WindowDecoration : public QObject
{
    Q_OBJECT

public:
    WindowDecoration(Toplevel *toplevel, const DecorationConfig &cfg,
                     AnimationManager *animMgr, QObject *parent = nullptr);
    ~WindowDecoration() override;

    void setConfig(const DecorationConfig &cfg);
    const DecorationConfig &config() const;

    void setFocused(bool focused); // triggers border animate if enabled

    // Called by LayoutManager/Compositor when window geometry changes (x,y,w,h are outer box including border+titlebar)
    void updateGeometry(int x, int y, int width, int height);

    // Titlebar toggle without recreating whole decoration (fast path)
    void setTitlebarEnabled(bool e);

    // Border helpers
    void setBorderWidth(int w);
    void setBorderRadius(int r);
    void setBorderColors(const QColor &active, const QColor &inactive);

    // Show/hide all nodes (for fullscreen/maximized handling)
    void setVisible(bool v);

private slots:
    void onBorderProgress(double eased);

private:
    void createNodes();
    void destroyNodes();
    void applyColorsImmediate(bool focused);
    void ensureAnimation();
    void updateRectColors(const QColor &c);
    static void colorToFloat4(const QColor &c, float out[4]);
    static QList<QColor> resolveGradientColors(const GradientConfig &g, const QColor &fallbackA, const QColor &fallbackB);

    Toplevel *m_tl = nullptr;
    DecorationConfig m_cfg;
    AnimationManager *m_animMgr = nullptr;
    Animation *m_borderAnim = nullptr;
    bool m_focused = false;
    double m_animT = 0; // 0=inactive,1=active

    // Scene nodes
    struct wlr_scene_tree *m_borderTree = nullptr;
    // We model border as 4 rects + optional corner rects for radius simulation; radius is stored but
    // actual rounded clipping relies on future scene clipping – for now we adjust rect sizes to leave corner cutouts blank.
    struct wlr_scene_rect *m_top = nullptr;
    struct wlr_scene_rect *m_bottom = nullptr;
    struct wlr_scene_rect *m_left = nullptr;
    struct wlr_scene_rect *m_right = nullptr;

    // Titlebar
    struct wlr_scene_tree *m_titleTree = nullptr;
    struct wlr_scene_rect *m_titleBg = nullptr;

    // Current geometry cache
    int m_x = 0, m_y = 0, m_w = 0, m_h = 0;
    bool m_visible = true;

    // Helper: blend two colors linearly
    static QColor lerpColor(const QColor &a, const QColor &b, double t);
};

// ---------- Manager that tracks all decorations ----------

class DecorationManager : public QObject
{
    Q_OBJECT
public:
    explicit DecorationManager(const DecorationConfig &cfg, AnimationManager *animMgr, QObject *parent = nullptr);

    void setConfig(const DecorationConfig &cfg);
    const DecorationConfig &config() const;

    WindowDecoration *decorationFor(Toplevel *tl) const;
    WindowDecoration *createFor(Toplevel *tl);
    void removeFor(Toplevel *tl);
    void setFocusedToplevel(Toplevel *tl); // updates all borders

    QList<WindowDecoration*> all() const;

private:
    DecorationConfig m_cfg;
    AnimationManager *m_animMgr = nullptr;
    QHash<Toplevel*, WindowDecoration*> m_decorations;
    Toplevel *m_focused = nullptr;
};
