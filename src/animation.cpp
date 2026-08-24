#include "animation.h"
#include "wlroots.h"
#include <QCoreApplication>
#include <cmath>
#include <algorithm>

struct wlr_box lerpBox(const struct wlr_box &a, const struct wlr_box &b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    struct wlr_box out;
    out.x = int(a.x + (b.x - a.x) * t);
    out.y = int(a.y + (b.y - a.y) * t);
    out.width = int(a.width + (b.width - a.width) * t);
    out.height = int(a.height + (b.height - a.height) * t);
    return out;
}
struct wlr_box easedLerpBox(const struct wlr_box &a, const struct wlr_box &b, double eased) {
    return lerpBox(a,b,eased);
}

QString Animation::id() const { return m_id; }
double Animation::progress() const { return m_progress; }
bool Animation::isRunning() const { return m_running; }
int Animation::duration() const { return m_durationMs; }
double Animation::speed() const { return m_localSpeed; }
Animation::Easing Animation::easing() const { return m_easing; }
void Animation::setEasing(Easing e) {
    m_easing = e;
}
bool Animation::loop() const { return m_loop; }
void Animation::setLoop(bool l) {
    m_loop = l;
}
Animation::Direction Animation::direction() const { return m_direction; }
void Animation::setDirection(Direction d) {
    m_direction = d;
}
void Animation::setUpdateCallback(std::function<void(double eased)> cb) {
    m_cb = std::move(cb);
}
void Animation::setFinishedCallback(std::function<void()> cb) {
    m_finishedCb = std::move(cb);
}

// ---- AnimationInstanceBase ----

AnimationInstanceBase::AnimationInstanceBase(Resource* target_, const std::string& kind_, QObject* parent)
    : Resource(parent), targetRes(target_), kind(kind_) {}
AnimationInstanceBase::~AnimationInstanceBase() {}
Resource* AnimationInstanceBase::target() const { return targetRes; }
std::string AnimationInstanceBase::kindName() const { return kind; }

// ---- Animation ----

Animation::Animation(const QString &id_, int durationMs, Easing easing, QObject *parent)
    : QObject(parent), m_id(id_), m_durationMs(durationMs), m_easing(easing)
{
}

void Animation::setDuration(int ms) {
    ms = std::max(0, ms);
    if (ms == m_durationMs)
        return;
    m_durationMs = ms;
    emit durationChanged(ms);
}

void Animation::setSpeed(double s) {
    if (s < 0)
        s = 0;
    if (s > 100)
        s = 100;
    if (qFuzzyCompare(s, m_localSpeed)
        ) return;
    m_localSpeed = s;
    emit speedChanged(s);
}

double Animation::easedProgress() const {
    return applyEasing(m_progress, m_easing);
}

void Animation::setProgress(double p) {
    p = std::clamp(p, 0.0, 1.0);
    if (qFuzzyCompare(p, m_progress)
        ) return;
    m_progress = p;
    emit rawProgressChanged(m_progress);
    double ep = easedProgress();
    emit progressChanged(ep);
    if (m_cb)
        m_cb(ep);
    if (m_progress >= 1.0 || m_progress <= 0.0) {
        if (m_finishedCb)
            m_finishedCb();
    }
}

void Animation::start() {
    if (m_running && !m_paused)
        return;
    m_progress = (m_direction == Direction::Forward) ? 0.0 : 1.0;
    m_running = true;
    m_paused = false;
    m_wallClock.start();
    emit runningChanged(true);
    emit started();
    emit rawProgressChanged(m_progress);
    emit progressChanged(easedProgress());
}

void Animation::stop() {
    if (!m_running && qFuzzyIsNull(m_progress)
        ) return;
    m_running = false;
    m_paused = false;
    emit runningChanged(false);
    emit finished();
    if (m_finishedCb)
        m_finishedCb();
}

void Animation::pause() {
    if (!m_running || m_paused)
        return;
    m_paused = true;
}

void Animation::resume() {
    if (!m_running || !m_paused)
        return;
    m_paused = false;
}

void Animation::restart() {
    stop();
    start();
}

void Animation::reverse() {
    m_direction = (m_direction == Direction::Forward) ? Direction::Backward : Direction::Forward;
    if (!m_running)
        start();
}

bool Animation::advance(double deltaMs) {
    if (!m_running || m_paused)
        return m_running;
    if (m_durationMs <= 0) {
        m_progress = (m_direction == Direction::Forward) ? 1.0 : 0.0;
        emit rawProgressChanged(m_progress);
        emit progressChanged(easedProgress());
        if (m_cb)
            m_cb(easedProgress());
        m_running = false;
        emit runningChanged(false);
        emit finished();
        if (m_finishedCb)
            m_finishedCb();
        return false;
    }
    double step = deltaMs / (double)m_durationMs;
    step *= m_localSpeed; // per-anim speed already; global speed is pre-applied by manager into deltaMs
    if (m_direction == Direction::Forward) {
        m_progress += step;
        if (m_progress >= 1.0) {
            m_progress = 1.0;
            emit rawProgressChanged(m_progress);
            double ep = easedProgress();
            emit progressChanged(ep);
            if (m_cb)
                m_cb(ep);
            if (m_loop) {
                m_progress = 0.0;
                emit rawProgressChanged(m_progress);
                emit progressChanged(easedProgress());
                return true;
            } else {
                m_running = false;
                emit runningChanged(false);
                emit finished();
                if (m_finishedCb)
                    m_finishedCb();
                return false;
            }
        }
    } else {
        m_progress -= step;
        if (m_progress <= 0.0) {
            m_progress = 0.0;
            emit rawProgressChanged(m_progress);
            double ep = easedProgress();
            emit progressChanged(ep);
            if (m_cb)
                m_cb(ep);
            if (m_loop) {
                m_progress = 1.0;
                emit rawProgressChanged(m_progress);
                emit progressChanged(easedProgress());
                return true;
            } else {
                m_running = false;
                emit runningChanged(false);
                emit finished();
                if (m_finishedCb)
                    m_finishedCb();
                return false;
            }
        }
    }
    emit rawProgressChanged(m_progress);
    double ep = easedProgress();
    emit progressChanged(ep);
    if (m_cb)
        m_cb(ep);
    return true;
}

Animation::Easing Animation::easingFromString(const QString &s, Easing fallback) {
    QString t = s.toLower().trimmed();
    if (t == "linear")
        return Easing::Linear;
    if (t == "easeinquad")
        return Easing::EaseInQuad;
    if (t == "easeoutquad")
        return Easing::EaseOutQuad;
    if (t == "easeinoutquad")
        return Easing::EaseInOutQuad;
    if (t == "easeincubic")
        return Easing::EaseInCubic;
    if (t == "easeoutcubic")
        return Easing::EaseOutCubic;
    if (t == "easeinoutcubic")
        return Easing::EaseInOutCubic;
    if (t == "easeinquart")
        return Easing::EaseInQuart;
    if (t == "easeoutquart")
        return Easing::EaseOutQuart;
    if (t == "easeinoutquart")
        return Easing::EaseInOutQuart;
    if (t == "easeoutback")
        return Easing::EaseOutBack;
    if (t == "easeoutelastic")
        return Easing::EaseOutElastic;
    // aliases
    if (t == "ease_in_cubic" || t == "ease-incubic")
        return Easing::EaseInCubic;
    if (t == "ease_out_cubic" || t == "ease-outcubic")
        return Easing::EaseOutCubic;
    if (t == "inquad")
        return Easing::EaseInQuad;
    if (t == "outcubic")
        return Easing::EaseOutCubic;
    return fallback;
}

QString Animation::easingToString(Easing e) {
    switch (e) {
    case Easing::Linear: return "linear";
    case Easing::EaseInQuad: return "easeInQuad";
    case Easing::EaseOutQuad: return "easeOutQuad";
    case Easing::EaseInOutQuad: return "easeInOutQuad";
    case Easing::EaseInCubic: return "easeInCubic";
    case Easing::EaseOutCubic: return "easeOutCubic";
    case Easing::EaseInOutCubic: return "easeInOutCubic";
    case Easing::EaseInQuart: return "easeInQuart";
    case Easing::EaseOutQuart: return "easeOutQuart";
    case Easing::EaseInOutQuart: return "easeInOutQuart";
    case Easing::EaseOutBack: return "easeOutBack";
    case Easing::EaseOutElastic: return "easeOutElastic";
    }
    return "linear";
}

double Animation::applyEasing(double t, Easing e) {
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case Easing::Linear: return t;
    case Easing::EaseInQuad: return t*t;
    case Easing::EaseOutQuad: return 1 - (1-t)*(1-t);
    case Easing::EaseInOutQuad: return t < 0.5 ? 2*t*t : 1 - std::pow(-2*t+2,2)/2.0;
    case Easing::EaseInCubic: return t*t*t;
    case Easing::EaseOutCubic: {
        double u=1-t;
        return 1 - u*u*u;
    }
    case Easing::EaseInOutCubic: return t < 0.5 ? 4*t*t*t : 1 - std::pow(-2*t+2,3)/2.0;
    case Easing::EaseInQuart: return t*t*t*t;
    case Easing::EaseOutQuart: {
        double u=1-t;
        return 1 - u*u*u*u;
    }
    case Easing::EaseInOutQuart: return t < 0.5 ? 8*t*t*t*t : 1 - std::pow(-2*t+2,4)/2.0;
    case Easing::EaseOutBack: {
        const double c1 = 1.70158;
        const double c3 = c1 + 1;
        double u = t - 1;
        return 1 + c3*u*u*u + c1*u*u;
    }
    case Easing::EaseOutElastic: {
        if (t == 0)
            return 0;
        if (t == 1)
            return 1;
        const double c4 = (2 * M_PI) / 3;
        return std::pow(2, -10*t) * std::sin((t*10 - 0.75)*c4) + 1;
    }
    }
    return t;
}

// ---- AnimationManager ----

bool AnimationManager::isEnabled() const { return m_enabled; }
double AnimationManager::globalSpeed() const { return m_globalSpeed; }
int AnimationManager::maxFps() const { return m_maxFps; }
QMap<QString,bool> AnimationManager::perAnimationToggles() const { return m_perAnimEnabled; }
AnimationManager *AnimationManager::instance() {
    return s_instance;
}

AnimationManager *AnimationManager::s_instance = nullptr;

AnimationManager::AnimationManager(QObject *parent) : QObject(parent) {
    s_instance = this;
    m_ticker = new QTimer(this);
    m_ticker->setTimerType(Qt::PreciseTimer);
    connect(m_ticker, &QTimer::timeout, this, &AnimationManager::onTick);
    updateTickerInterval();
    m_elapsed.start();
    m_lastTickNs = m_elapsed.nsecsElapsed();
}

AnimationManager::~AnimationManager() {
    if (s_instance == this)
        s_instance = nullptr;
}

void AnimationManager::setEnabled(bool e) {
    if (e == m_enabled)
        return;
    m_enabled = e;
    emit enabledChanged(e);
    if (!e) {
        // stop ticker if no need; animations stay but won't advance
        if (m_ticker->isActive()
            ) m_ticker->stop();
        m_ticking = false;
    } else {
        ensureTicker();
    }
}

void AnimationManager::setGlobalSpeed(double s) {
    s = std::clamp(s, 0.0, 10.0);
    if (qFuzzyCompare(s, m_globalSpeed)
        ) return;
    m_globalSpeed = s;
    emit globalSpeedChanged(s);
}

void AnimationManager::setMaxFps(int fps) {
    if (fps < 0)
        fps = 0;
    if (fps > 1000)
        fps = 1000;
    if (fps == m_maxFps)
        return;
    m_maxFps = fps;
    emit maxFpsChanged(fps);
    updateTickerInterval();
}

bool AnimationManager::isAnimationEnabled(const QString &id) const {
    auto it = m_perAnimEnabled.find(id);
    if (it == m_perAnimEnabled.end()
        ) return true;
        // default enabled;
    return it.value();
}

void AnimationManager::setAnimationEnabled(const QString &id, bool enabled) {
    m_perAnimEnabled[id] = enabled;
    if (!enabled) {
        if (auto *a = get(id)
            ) a->stop();
    }
}

Animation *AnimationManager::get(const QString &id) const {
    return m_anims.value(id, nullptr);
}

Animation *AnimationManager::create(const QString &id, int durationMs, Animation::Easing easing) {
    if (auto *ex = get(id)
        ) return ex;
    auto *a = new Animation(id, durationMs, easing, const_cast<AnimationManager*>(this));
    add(a);
    return a;
}

void AnimationManager::add(Animation *anim) {
    if (!anim)
        return;
    QString id = anim->id();
    if (m_anims.contains(id)
        ) return;
    if (anim->parent()
        != this) anim->setParent(this);
    m_anims.insert(id, anim);
    connect(anim, &Animation::started, this, [this]() {
        ensureTicker();
    });
    connect(anim, &Animation::finished, this, [this, anim]() {
        onAnimFinished(anim);
    });
    emit animationAdded(anim);
    if (anim->isRunning()
        ) ensureTicker();
}

bool AnimationManager::remove(const QString &id) {
    auto it = m_anims.find(id);
    if (it == m_anims.end()
        ) return false;
    Animation *a = it.value();
    m_anims.erase(it);
    emit animationRemoved(id);
    a->deleteLater();
    if (m_anims.isEmpty()) {
        // no animations left, could stop but keep ticker idle stop in onTick
    }
    return true;
}

QList<Animation*> AnimationManager::activeAnimations() const {
    QList<Animation*> out;
    for (auto *a : m_anims) if (a->isRunning()) out << a;
    return out;
}

QList<Animation*> AnimationManager::allAnimations() const {
    return m_anims.values();
}

void AnimationManager::ensureTicker() {
    if (!m_enabled)
        return;
    if (m_globalSpeed == 0.0)
        return;
    // any running anim and its per-id enabled?
    bool any = false;
    for (auto *a : m_anims) {
if (a->isRunning() && isAnimationEnabled(a->id())) {
            any = true;
            break;
        }
    }
    if (!any) {
        // we still start ticker if we were just asked to (started signal) – onTick will stop if none
        // but for now don't start if nothing running
        return;
    }
    if (!m_ticker->isActive()) {
        m_lastTickNs = m_elapsed.nsecsElapsed();
        m_ticker->start();
        m_ticking = true;
    }
}

void AnimationManager::updateTickerInterval() {
    if (m_maxFps <= 0) {
        m_ticker->setInterval(1); // ~1000 Hz uncapped
    } else {
        int iv = 1000 / m_maxFps;
        if (iv < 1)
            iv = 1;
        if (iv > 1000)
            iv = 1000;
        m_ticker->setInterval(iv);
    }
}

void AnimationManager::onTick() {
    if (!m_enabled || m_globalSpeed == 0.0) {
        m_ticker->stop();
        m_ticking = false;
        return;
    }
    qint64 nowNs = m_elapsed.nsecsElapsed();
    double deltaMs = (nowNs - m_lastTickNs) / 1e6;
    m_lastTickNs = nowNs;
    if (deltaMs > 100)
        deltaMs = 100;
        // clamp huge jumps (suspend);
    double scaled = deltaMs * m_globalSpeed;

    bool anyRunning = false;
    // copy list because animations may be removed during iteration
    auto anims = m_anims.values();
    for (Animation *a : anims) {
        if (!a->isRunning()
            ) continue;
        if (!isAnimationEnabled(a->id()
            )) continue;
        bool still = a->advance(scaled);
        if (still)
            anyRunning = true;
    }
    if (anyRunning) {
        emit frameRequested(); // drive output at max FPS while animating
    } else {
        // check if any still running (maybe per-anim disabled filtering)
for (Animation *a : m_anims) if (a->isRunning() && isAnimationEnabled(a->id())) {
            anyRunning = true;
            break;
        }
        if (!anyRunning) {
            m_ticker->stop();
            m_ticking = false;
        } else {
            emit frameRequested();
        }
    }
}

void AnimationManager::onAnimFinished(Animation *) {
    // ticker stop logic handled in next onTick
}
