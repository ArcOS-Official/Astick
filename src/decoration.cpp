#include "decoration.h"
#include "toplevel.h"
#include "animation.h"
#include <QColor>
#include <cmath>
#include <algorithm>

QColor GradientConfig::sample(double t) const {
    if (colors.size()
        < 2) return colors.isEmpty() ? QColor("#000000") : colors.first();
    t = std::clamp(t, 0.0, 1.0);
    double scaled = t * (colors.size() - 1);
    int i = (int)scaled;
    double f = scaled - i;
    if (i >= colors.size()
        - 1) return colors.last();
    auto a = colors[i];
    auto b = colors[i+1];
    return QColor::fromRgbF(a.redF()*(1-f)+b.redF()*f, a.greenF()*(1-f)+b.greenF()*f, a.blueF()*(1-f)+b.blueF()*f, a.alphaF()*(1-f)+b.alphaF()*f);
}

QColor WindowDecoration::lerpColor(const QColor &a, const QColor &b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF()*(1-t)+b.redF()*t, a.greenF()*(1-t)+b.greenF()*t, a.blueF()*(1-t)+b.blueF()*t, a.alphaF()*(1-t)+b.alphaF()*t);
}

void WindowDecoration::colorToFloat4(const QColor &c, float out[4]) {
    out[0] = c.redF(); out[1] = c.greenF(); out[2] = c.blueF(); out[3] = c.alphaF();
}

QList<QColor> WindowDecoration::resolveGradientColors(const GradientConfig &g, const QColor &fallbackA, const QColor &fallbackB) {
if (!g.enabled || g.colors.size() < 2) return {
    }
    ;
    return g.colors;
}

WindowDecoration::WindowDecoration(Toplevel *toplevel, const DecorationConfig &cfg, AnimationManager *animMgr, QObject *parent)
    : QObject(parent), m_tl(toplevel), m_cfg(cfg), m_animMgr(animMgr) {
    createNodes();
    ensureAnimation();
    applyColorsImmediate(false);
}

WindowDecoration::~WindowDecoration() {
    destroyNodes();
}

void WindowDecoration::setConfig(const DecorationConfig &cfg) {
    bool needRecreate = (cfg.border.enabled != m_cfg.border.enabled) || (cfg.titlebar.enabled != m_cfg.titlebar.enabled);
    m_cfg = cfg;
if (needRecreate) {
        destroyNodes();
        createNodes();
        ensureAnimation();
    }
    else { ensureAnimation(); }
    applyColorsImmediate(m_focused);
    updateGeometry(m_x, m_y, m_w, m_h);
}
const DecorationConfig &WindowDecoration::config() const { return m_cfg; }

void WindowDecoration::setFocused(bool focused) {
    if (m_focused == focused)
        return;
    m_focused = focused;
    if (!m_cfg.border.enabled)
        return;
    if (!m_cfg.border.animate || !m_animMgr || !m_animMgr->isEnabled() || !m_animMgr->isAnimationEnabled("border") || m_cfg.border.animationDuration <= 0) {
        m_animT = focused ? 1.0 : 0.0;
        applyColorsImmediate(focused);
        return;
    }
    ensureAnimation();
if (!m_borderAnim) {
        applyColorsImmediate(focused);
        return;
    }
    m_borderAnim->setDuration(m_cfg.border.animationDuration);
    m_borderAnim->setEasing(Animation::easingFromString(m_cfg.border.animationEasing));
    // animate from current T to target
    double target = focused ? 1.0 : 0.0;
    // configure direction: we always interpolate 0->1 raw, T reflects eased intermediate. Instead use progress mapping.
    // Simplest: restart anim that goes from current m_animT to target using eased progress.
    // We'll set progress to current, then animate forward.
    m_borderAnim->stop();
    // Use custom advance: set progress to current, then animate to target by using direction and manual interpolation.
    // Easier: create new animation on each focus switch that lerps.
    // Instead we reuse single anim: set progress to 0 and lerp in callback.
    // Store start/end for interpolation
    double start = m_animT;
    m_borderAnim->setUpdateCallback([this, start, target](double eased){
        double t = start + (target - start) * eased;
        m_animT = std::clamp(t, 0.0, 1.0);
        QColor c = lerpColor(m_cfg.border.inactiveColor, m_cfg.border.activeColor, m_animT);
        if (m_cfg.border.gradient.enabled && m_cfg.border.gradient.colors.size() >= 2) {
            // if gradient enabled, blend gradient sample with focus
            QColor g = m_cfg.border.gradient.sample(m_animT);
            c = lerpColor(c, g, 0.5);
        }
        updateRectColors(c);
    });
    m_borderAnim->start();
}

void WindowDecoration::updateGeometry(int x, int y, int w, int h) {
    m_x = x; m_y = y; m_w = w; m_h = h;
    if (!m_borderTree)
        return;
    int bw = m_cfg.border.enabled ? m_cfg.border.width : 0;
    int th = m_cfg.titlebar.enabled ? m_cfg.titlebar.height : 0;
    int radius = m_cfg.border.radius;
    (void)radius;
    // position border tree behind window
    // total outer: (w+2*bw) x (h+2*bw+th)
    int outerX = x - bw;
    int outerY = y - bw - th;
    int outerW = w + 2*bw;
    int outerH = h + 2*bw + th;
    if (m_borderTree) {
        wlr_scene_node_set_position(&m_borderTree->node, outerX, outerY);
    }
    if (m_cfg.border.enabled) {
        float dummy[4]={0,0,0,1};
        (void)dummy;
        // top (includes titlebar area? we keep title separate)
        // border strips: top, bottom, left, right
        // top strip under titlebar or above window
        if (m_top) {
            wlr_scene_rect_set_size(m_top, outerW, bw);
            wlr_scene_node_set_position(&m_top->node, 0, th);
        }
        if (m_bottom) {
            wlr_scene_rect_set_size(m_bottom, outerW, bw);
            wlr_scene_node_set_position(&m_bottom->node, 0, th + bw + h);
        }
        if (m_left) {
            wlr_scene_rect_set_size(m_left, bw, h);
            wlr_scene_node_set_position(&m_left->node, 0, th + bw);
        }
        if (m_right) {
            wlr_scene_rect_set_size(m_right, bw, h);
            wlr_scene_node_set_position(&m_right->node, outerW - bw, th + bw);
        }
        // radius: we simply inset corners by radius to simulate rounded look (clip not real)
        // For now radius just reduces rect lengths slightly; true rounded clipping would need shader.
        if (radius > 0) {
            // no-op visual, but keep config stored for future renderer
        }
    }
    if (m_titleTree && m_titleBg) {
        wlr_scene_node_set_position(&m_titleTree->node, 0, 0);
        wlr_scene_rect_set_size(m_titleBg, outerW, th);
    }
    Q_UNUSED(outerH);
}

void WindowDecoration::setTitlebarEnabled(bool e) {
    if (m_cfg.titlebar.enabled == e)
        return;
    m_cfg.titlebar.enabled = e;
    destroyNodes(); createNodes(); applyColorsImmediate(m_focused); updateGeometry(m_x,m_y,m_w,m_h);
}

void WindowDecoration::setBorderWidth(int w) {
    m_cfg.border.width = std::max(0,w);
    updateGeometry(m_x,m_y,m_w,m_h);
}
void WindowDecoration::setBorderRadius(int r) {
    m_cfg.border.radius = std::max(0,r);
    updateGeometry(m_x,m_y,m_w,m_h);
}
void WindowDecoration::setBorderColors(const QColor &active, const QColor &inactive){
    m_cfg.border.activeColor=active;
    m_cfg.border.inactiveColor=inactive;
    applyColorsImmediate(m_focused);
}
void WindowDecoration::setVisible(bool v){
    m_visible=v;
    if(m_borderTree)
        wlr_scene_node_set_enabled(&m_borderTree->node, v && (m_cfg.border.enabled || m_cfg.titlebar.enabled));
    // keep toplevel itself visibility handled by layout
}

void WindowDecoration::onBorderProgress(double eased){
    m_animT = std::clamp(eased,0.0,1.0);
    QColor c = lerpColor(m_cfg.border.inactiveColor, m_cfg.border.activeColor, m_animT);
    updateRectColors(c);
}

void WindowDecoration::createNodes(){
    if (!m_tl || !m_tl->getSceneTree()
        ) return;
    struct wlr_scene_tree *parentTree = m_tl->getSceneTree()->node.parent;
    if (!parentTree)
        return;
    // border tree sibling to toplevel tree, placed just below it
    m_borderTree = wlr_scene_tree_create(parentTree);
    if (!m_borderTree)
        return;
    // place below toplevel
    wlr_scene_node_place_below(&m_borderTree->node, &m_tl->getSceneTree()->node);
    if (m_cfg.border.enabled) {
        float col[4]; colorToFloat4(m_cfg.border.inactiveColor, col);
        m_top = wlr_scene_rect_create(m_borderTree, 1, 1, col);
        m_bottom = wlr_scene_rect_create(m_borderTree, 1, 1, col);
        m_left = wlr_scene_rect_create(m_borderTree, 1, 1, col);
        m_right = wlr_scene_rect_create(m_borderTree, 1, 1, col);
    }
    if (m_cfg.titlebar.enabled) {
        m_titleTree = wlr_scene_tree_create(m_borderTree);
        float tcol[4]; colorToFloat4(m_cfg.titlebar.color, tcol);
        m_titleBg = wlr_scene_rect_create(m_titleTree, 1, 1, tcol);
    }
    if (!m_visible)
        wlr_scene_node_set_enabled(&m_borderTree->node, false);
}

void WindowDecoration::destroyNodes(){
if (m_borderTree) {
        wlr_scene_node_destroy(&m_borderTree->node);
        m_borderTree=nullptr;
        m_top=m_bottom=m_left=m_right=nullptr;
        m_titleTree=nullptr;
        m_titleBg=nullptr;
    }
}

void WindowDecoration::applyColorsImmediate(bool focused){
    m_animT = focused ? 1.0 : 0.0;
    QColor c = focused ? m_cfg.border.activeColor : m_cfg.border.inactiveColor;
    if (m_cfg.border.gradient.enabled && m_cfg.border.gradient.colors.size()>=2) {
        // sample gradient at focus t
        c = m_cfg.border.gradient.sample(m_animT);
    }
    updateRectColors(c);
}

void WindowDecoration::ensureAnimation(){
    if (!m_animMgr)
        return;
if (!m_cfg.border.animate) {
        if(m_borderAnim) m_borderAnim->stop();
        return;
    }
    if (m_borderAnim)
        return;
    QString id = QString("border:%1").arg((quintptr)m_tl);
    m_borderAnim = m_animMgr->get(id);
    if (!m_borderAnim) {
        m_borderAnim = m_animMgr->create(id, m_cfg.border.animationDuration, Animation::easingFromString(m_cfg.border.animationEasing));
    }
    connect(m_borderAnim, &Animation::progressChanged, this, &WindowDecoration::onBorderProgress, Qt::UniqueConnection);
}

void WindowDecoration::updateRectColors(const QColor &c){
    float f[4]; colorToFloat4(c,f);
    if(m_top)
        wlr_scene_rect_set_color(m_top,f);
    if(m_bottom)
        wlr_scene_rect_set_color(m_bottom,f);
    if(m_left)
        wlr_scene_rect_set_color(m_left,f);
    if(m_right)
        wlr_scene_rect_set_color(m_right,f);
}

// DecorationManager
DecorationManager::DecorationManager(const DecorationConfig &cfg, AnimationManager *animMgr, QObject *parent)
    : QObject(parent), m_cfg(cfg), m_animMgr(animMgr) {}
const DecorationConfig &DecorationManager::config() const { return m_cfg; }
QList<WindowDecoration*> DecorationManager::all() const { return m_decorations.values(); }
void DecorationManager::setConfig(const DecorationConfig &cfg){
    m_cfg=cfg;
    for(auto *d: m_decorations) d->setConfig(cfg);
}
WindowDecoration* DecorationManager::decorationFor(Toplevel *tl) const { return m_decorations.value(tl,nullptr); }
WindowDecoration* DecorationManager::createFor(Toplevel *tl){
    if(!tl)
        return nullptr;
    if(m_decorations.contains(tl)
        ) return m_decorations[tl];
    auto *d = new WindowDecoration(tl, m_cfg, m_animMgr, const_cast<DecorationManager*>(this));
    m_decorations.insert(tl,d);
    if(tl) connect(tl, &Toplevel::destroyed, this, [this,tl](){
        removeFor(tl);
    });
    return d;
}
void DecorationManager::removeFor(Toplevel *tl){
    auto it=m_decorations.find(tl);
    if(it==m_decorations.end()
        ) return;
    WindowDecoration *d=it.value(); m_decorations.erase(it); d->deleteLater();
}
void DecorationManager::setFocusedToplevel(Toplevel *tl){
    if(m_focused==tl)
        return;
    if(m_focused){
        if(auto *d=decorationFor(m_focused)) d->setFocused(false);
    }
    m_focused=tl;
    if(m_focused){
        auto *d=decorationFor(m_focused);
        if(!d) d=createFor(m_focused);
        d->setFocused(true);
    }
    // unfocus others
    for(auto it=m_decorations.begin(); it!=m_decorations.end(); ++it){
        if(it.key()
            !=m_focused) it.value()->setFocused(false);
    }
}
