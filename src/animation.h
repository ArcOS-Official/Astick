#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QString>
#include <functional>
#include <optional>
#include <type_traits>
#include <array>
#include <unordered_map>
#include <chrono>
#include "resource.h"
#include "config.h"
#include "wlroots.h"

struct wlr_box;

class Animation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    enum class Easing {
        Linear,
        EaseInQuad, EaseOutQuad, EaseInOutQuad,
        EaseInCubic, EaseOutCubic, EaseInOutCubic,
        EaseInQuart, EaseOutQuart, EaseInOutQuart,
        EaseOutBack, EaseOutElastic,
    };
    Q_ENUM(Easing)

    enum class Direction { Forward, Backward };
    Q_ENUM(Direction)

    explicit Animation(const QString &id_, int durationMs = 250,
                       Easing easing = Easing::EaseOutCubic,
                       QObject *parent = nullptr);

    QString id() const;
    double progress() const;
    double easedProgress() const;
    bool isRunning() const;
    int duration() const;
    void setDuration(int ms);
    double speed() const;
    void setSpeed(double s);

    Easing easing() const;
    void setEasing(Easing e);

    bool loop() const;
    void setLoop(bool l);

    Direction direction() const;
    void setDirection(Direction d);

    static Easing easingFromString(const QString &s, Easing fallback = Easing::EaseOutCubic);
    static QString easingToString(Easing e);
    static double applyEasing(double t, Easing e);

    void setUpdateCallback(std::function<void(double eased)> cb);
    void setFinishedCallback(std::function<void()> cb);

public slots:
    void start();
    void stop();
    void pause();
    void resume();
    void restart();
    void setProgress(double p);
    void reverse();

signals:
    void progressChanged(double easedProgress);
    void rawProgressChanged(double rawProgress);
    void started();
    void finished();
    void runningChanged(bool running);
    void durationChanged(int ms);
    void speedChanged(double speed);

private:
    friend class AnimationManager;
    bool advance(double deltaMs);

    QString m_id;
    int m_durationMs = 250;
    Easing m_easing = Easing::EaseOutCubic;
    double m_progress = 0.0;
    double m_localSpeed = 1.0;
    bool m_running = false;
    bool m_paused = false;
    bool m_loop = false;
    Direction m_direction = Direction::Forward;
    QElapsedTimer m_wallClock;

    std::function<void(double)> m_cb;
    std::function<void()> m_finishedCb;
};

inline double lerpVal(double a, double b, double t) { return a + (b - a) * t; }
inline int lerpVal(int a, int b, double t) { return int(a + (b - a) * t); }
inline float lerpVal(float a, float b, double t) { return float(a + (b - a) * t); }
template<typename T>
inline void lerpArray(T **ptrs, T *aVals, T *bVals, int count, double t) {
    for(int i=0;i<count;i++) *ptrs[i] = T(aVals[i] + (bVals[i] - aVals[i]) * t);
}
struct wlr_box lerpBox(const struct wlr_box &a, const struct wlr_box &b, double t);
struct wlr_box easedLerpBox(const struct wlr_box &a, const struct wlr_box &b, double eased);

class AnimationManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double globalSpeed READ globalSpeed WRITE setGlobalSpeed NOTIFY globalSpeedChanged)
    Q_PROPERTY(int maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)

public:
    explicit AnimationManager(QObject *parent = nullptr);
    ~AnimationManager() override;

    static AnimationManager *instance();

    bool isEnabled() const;
    void setEnabled(bool e);
    double globalSpeed() const;
    void setGlobalSpeed(double s);
    int maxFps() const;
    void setMaxFps(int fps);

    bool isAnimationEnabled(const QString &id) const;
    void setAnimationEnabled(const QString &id, bool enabled);
    QMap<QString,bool> perAnimationToggles() const;

    Animation *get(const QString &id) const;
    Animation *create(const QString &id, int durationMs = 250,
                      Animation::Easing easing = Animation::Easing::EaseOutCubic);
    void add(Animation *anim);
    bool remove(const QString &id);
    QList<Animation*> activeAnimations() const;
    QList<Animation*> allAnimations() const;

signals:
    void enabledChanged(bool e);
    void globalSpeedChanged(double s);
    void maxFpsChanged(int fps);
    void frameRequested();
    void animationAdded(Animation *a);
    void animationRemoved(const QString &id);

private slots:
    void onTick();

private:
    void ensureTicker();
    void updateTickerInterval();
    void onAnimFinished(Animation *anim);

    static AnimationManager *s_instance;
    QHash<QString, Animation*> m_anims;
    QMap<QString,bool> m_perAnimEnabled;
    QTimer *m_ticker = nullptr;
    QElapsedTimer m_elapsed;
    bool m_enabled = true;
    double m_globalSpeed = 1.0;
    int m_maxFps = 60;
    bool m_ticking = false;
    qint64 m_lastTickNs = 0;
};

// AnimationPool family

class AnimationInstanceBase : public Resource {
    Q_OBJECT
public:
    explicit AnimationInstanceBase(Resource* target_, const std::string& kind_, QObject* parent = nullptr);
    virtual ~AnimationInstanceBase();
    virtual bool tick(uint64_t nowMs) = 0;
    virtual Resource* target() const;
    std::string kindName() const;
    uint64_t startTimeMs = 0;
    AnimDef def;
    std::function<void()> applyCallback;
protected:
    Resource* targetRes = nullptr;
    std::string kind;
};

template<typename T>
class AnimationInstance : public AnimationInstanceBase {
public:
    AnimationInstance(Resource* target_, const std::string& kind_, std::array<T*,10> ptrs_, std::array<T,10> starts_, std::array<T,10> targets_, int count_, AnimDef def_, uint64_t startMs, std::function<void()> applyCb = nullptr, QObject* parent = nullptr);
    uint64_t genId() override;
    bool tick(uint64_t nowMs) override;
    std::array<T*,10> ptrs{};
    std::array<T,10> startVals{};
    std::array<T,10> targets{};
    int count = 0;
};

class AnimationPool : public QObject {
    Q_OBJECT
public:
    explicit AnimationPool(Config* cfg = nullptr, QObject* parent = nullptr);
    ~AnimationPool();

    void setConfig(Config* cfg);
    void reloadPresets();

    template<typename T>
    AnimationInstance<T>* addInstance(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const std::string& kind, std::function<void()> applyCb = nullptr);

    template<typename T>
    AnimationInstance<T>* addInstance(Resource& target, std::initializer_list<std::reference_wrapper<T>> refs, std::initializer_list<T> targetVals, const std::string& kind, std::function<void()> applyCb = nullptr);

    template<typename T>
    AnimationInstance<T>* addInstance(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const std::string& kind);

    template<typename T>
    AnimationInstance<T>* addInstanceWithDef(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const AnimDef& def, std::function<void()> applyCb = nullptr);

    void removeAnimation(uint64_t resourceId);
    void removeAnimation(AnimationInstanceBase* inst);
    void tickAll(uint64_t nowMs);
    void onFrame();

    size_t size() const;
    bool hasAnimationFor(uint64_t resId) const;
    uint64_t nowMs() const;

signals:
    void frameRequested();

private:
    std::unordered_map<uint64_t, AnimationInstanceBase*> pool;
    Config* config = nullptr;
};
