#include "animation.h"
#include <chrono>
#include <ctime>

AnimationPool::AnimationPool(Config* cfg, QObject* parent) : QObject(parent), config(cfg) {}
AnimationPool::~AnimationPool() {
    for(auto &kv : pool) kv.second->deleteLater();
    pool.clear();
}

void AnimationPool::setConfig(Config* cfg) { config = cfg; }
size_t AnimationPool::size() const { return pool.size(); }
bool AnimationPool::hasAnimationFor(uint64_t resId) const { return pool.find(resId)!=pool.end(); }

uint64_t AnimationPool::nowMs() const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec)*1000 + ts.tv_nsec/1000000;
}

void AnimationPool::removeAnimation(uint64_t resourceId) {
    auto it = pool.find(resourceId);
    if(it==pool.end()) return;
    AnimationInstanceBase* inst = it->second;
    pool.erase(it);
    inst->disconnect(this);
    inst->deleteLater();
}
void AnimationPool::removeAnimation(AnimationInstanceBase* inst) {
    if(!inst) return;
    for(auto it=pool.begin(); it!=pool.end(); ++it){
        if(it->second==inst){ pool.erase(it); inst->deleteLater(); break; }
    }
}

void AnimationPool::tickAll(uint64_t now) {
    if (pool.empty()) {
        wlr_log(WLR_INFO, "AnimationPool::tickAll now=%lu poolSize=0 (empty, no tick)", (unsigned long)now);
        return;
    }
    wlr_log(WLR_INFO, "AnimationPool::tickAll now=%lu poolSize=%zu", (unsigned long)now, pool.size());
    std::vector<uint64_t> doneIds;
    for(auto &kv : pool){
        AnimationInstanceBase* inst = kv.second;
        bool still = inst->tick(now);
        wlr_log(WLR_INFO, "  tick inst %p target %p id %lu kind %s still=%d", (void*)inst, (void*)inst->target(), (unsigned long)kv.first, inst->kindName().c_str(), still);
        if(!still) doneIds.push_back(kv.first);
    }
    for(uint64_t id : doneIds){
        auto it = pool.find(id);
        if(it!=pool.end()){
            AnimationInstanceBase* inst = it->second;
            wlr_log(WLR_INFO, "AnimationPool: animation done for resource %lu kind %s, removing", (unsigned long)id, inst->kindName().c_str());
            pool.erase(it);
            inst->deleteLater();
        }
    }
    if(!pool.empty()){
        wlr_log(WLR_INFO, "AnimationPool::tickAll emitting frameRequested poolSize %zu", pool.size());
        emit frameRequested();
    } else {
        wlr_log(WLR_INFO, "AnimationPool: all done");
    }
}

void AnimationPool::onFrame() {
    wlr_log(WLR_INFO, "AnimationPool::onFrame poolSize %zu", pool.size());
    tickAll(nowMs());
}

void AnimationPool::reloadPresets() {
}

// Template implementations

template<typename T>
AnimationInstance<T>::AnimationInstance(Resource* target_, const std::string& kind_, std::array<T*,10> ptrs_, std::array<T,10> starts_, std::array<T,10> targets_, int count_, AnimDef def_, uint64_t startMs, std::function<void()> applyCb, QObject* parent)
    : AnimationInstanceBase(target_, kind_, parent), count(count_) {
    def = def_;
    for(int i=0;i<10;i++) { ptrs[i]=ptrs_[i]; startVals[i]=starts_[i]; targets[i]=targets_[i]; }
    startTimeMs = startMs;
    applyCallback = applyCb;
    generateId();
    if (target_) {
        connect(target_, &Resource::resourceDestroyed, this, [this](Resource*){
            this->deleteLater();
        });
    }
}

template<typename T>
uint64_t AnimationInstance<T>::genId() {
    return allocateId(ResourceKind::AnimationBase);
}

template<typename T>
bool AnimationInstance<T>::tick(uint64_t nowMs_) {
    if (!targetRes) return false;
    uint64_t elapsed = nowMs_ >= startTimeMs ? nowMs_ - startTimeMs : 0;
    double t = def.duration > 0 ? double(elapsed) / double(def.duration) : 1.0;
    if (t >= 1.0) t = 1.0;
    double eased = Animation::applyEasing(t, Animation::easingFromString(QString::fromStdString(def.easing), Animation::Easing::EaseOutCubic));
    wlr_log(WLR_INFO, "AnimationInstance tick target %lu kind %s t=%.3f eased=%.3f count=%d", (unsigned long)targetRes->id, kind.c_str(), t, eased, count);
    for(int i=0;i<count;i++) if(ptrs[i]) {
        *ptrs[i] = T(startVals[i] + (targets[i] - startVals[i]) * eased);
    }
    if (applyCallback) applyCallback();
    if (t >= 1.0) {
        for(int i=0;i<count;i++) if(ptrs[i]) *ptrs[i] = targets[i];
        if (applyCallback) applyCallback();
        return false;
    }
    return true;
}

template<typename T>
AnimationInstance<T>* AnimationPool::addInstance(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const std::string& kind, std::function<void()> applyCb) {
    uint64_t resId = target.id;
    wlr_log(WLR_INFO, "AnimationPool::addInstance target %lu kind %s count %d poolSize %zu", (unsigned long)resId, kind.c_str(), count, pool.size());
    for(int i=0;i<count;i++) if(ptrs[i]) wlr_log(WLR_INFO, "  ptr[%d]=%p val %d -> target %d", i, (void*)ptrs[i], (int)*ptrs[i], (int)targets[i]);
    auto it = pool.find(resId);
    if (it != pool.end()) {
        wlr_log(WLR_INFO, "  overwriting existing animation for resource %lu", (unsigned long)resId);
        AnimationInstanceBase* old = it->second;
        pool.erase(it);
        old->deleteLater();
    }
    AnimDef def;
    bool hasDef = false;
    if (config) {
        auto pit = config->animations.pairs.find(kind);
        if (pit != config->animations.pairs.end()) {
            def = pit->second.start;
            hasDef = true;
        }
    }
    if (!hasDef) {
        def.enabled = true;
        def.duration = 250;
        def.easing = "easeOutCubic";
        def.style = AnimationStyle::Fade;
    }
    wlr_log(WLR_INFO, "  using def duration %d easing %s enabled %d", def.duration, def.easing.c_str(), def.enabled);
    if (!def.enabled || def.duration==0) {
        for(int i=0;i<count;i++) if(ptrs[i]) *ptrs[i]=targets[i];
        if(applyCb) applyCb();
        return nullptr;
    }
    std::array<T,10> starts{};
    for(int i=0;i<count;i++) starts[i] = ptrs[i] ? *ptrs[i] : T{};
    uint64_t now = nowMs();
    auto *inst = new AnimationInstance<T>(&target, kind, ptrs, starts, targets, count, def, now, applyCb, this);
    connect(inst, &Resource::resourceDestroyed, this, [this, inst](Resource*){
        for(auto it2 = pool.begin(); it2 != pool.end(); ++it2){
            if(it2->second == inst){ pool.erase(it2); break; }
        }
    });
    connect(&target, &Resource::resourceDestroyed, this, [this, resId](Resource*){
        auto it3 = pool.find(resId);
        if(it3 != pool.end()){
            AnimationInstanceBase* a = it3->second;
            pool.erase(it3);
            a->deleteLater();
        }
    });
    pool.emplace(resId, inst);
    return inst;
}

template<typename T>
AnimationInstance<T>* AnimationPool::addInstance(Resource& target, std::initializer_list<std::reference_wrapper<T>> refs, std::initializer_list<T> targetVals, const std::string& kind, std::function<void()> applyCb) {
    std::array<T*,10> ptrs{}; std::array<T,10> targs{};
    int count = 0;
    auto itR = refs.begin();
    auto itT = targetVals.begin();
    for(; itR != refs.end() && itT != targetVals.end() && count < 10; ++itR, ++itT, ++count){
        ptrs[count] = &itR->get();
        targs[count] = *itT;
    }
    return addInstance<T>(target, ptrs, targs, count, kind, applyCb);
}

template<typename T>
AnimationInstance<T>* AnimationPool::addInstance(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const std::string& kind) {
    return addInstance<T>(target, ptrs, targets, count, kind, nullptr);
}

template<typename T>
AnimationInstance<T>* AnimationPool::addInstanceWithDef(Resource& target, std::array<T*,10> ptrs, std::array<T,10> targets, int count, const AnimDef& def, std::function<void()> applyCb) {
    wlr_log(WLR_INFO, "AnimationPool::addInstanceWithDef target %lu id %lu count %d duration %d easing %s", (unsigned long)target.id, (unsigned long)target.id, count, def.duration, def.easing.c_str());
    for(int i=0;i<count;i++) if(ptrs[i]) wlr_log(WLR_INFO, "  [%d] %p %d -> %d", i, (void*)ptrs[i], (int)*ptrs[i], (int)targets[i]);
    uint64_t resId = target.id;
    auto it = pool.find(resId);
    if (it != pool.end()) {
        wlr_log(WLR_INFO, "  overwriting existing for %lu", (unsigned long)resId);
        AnimationInstanceBase* old = it->second;
        pool.erase(it);
        old->deleteLater();
    }
    if (!def.enabled || def.duration==0) {
        wlr_log(WLR_INFO, "  def disabled or zero duration, applying directly");
        for(int i=0;i<count;i++) if(ptrs[i]) *ptrs[i]=targets[i];
        if(applyCb) applyCb();
        return nullptr;
    }
    std::array<T,10> starts{};
    for(int i=0;i<count;i++) starts[i] = ptrs[i] ? *ptrs[i] : T{};
    uint64_t now = nowMs();
    auto *inst = new AnimationInstance<T>(&target, "custom", ptrs, starts, targets, count, def, now, applyCb, this);
    connect(inst, &Resource::resourceDestroyed, this, [this, inst](Resource*){
        for(auto it = pool.begin(); it != pool.end(); ++it){
            if(it->second == inst){ pool.erase(it); break; }
        }
    });
    connect(&target, &Resource::resourceDestroyed, this, [this, resId](Resource*){
        auto it3 = pool.find(resId);
        if(it3 != pool.end()){
            AnimationInstanceBase* a = it3->second;
            pool.erase(it3);
            a->deleteLater();
        }
    });
    pool.emplace(resId, inst);
    return inst;
}

// Explicit instantiations
template class AnimationInstance<int>;
template class AnimationInstance<float>;
template class AnimationInstance<double>;

template AnimationInstance<int>* AnimationPool::addInstance<int>(Resource&, std::array<int*,10>, std::array<int,10>, int, const std::string&, std::function<void()>);
template AnimationInstance<float>* AnimationPool::addInstance<float>(Resource&, std::array<float*,10>, std::array<float,10>, int, const std::string&, std::function<void()>);
template AnimationInstance<double>* AnimationPool::addInstance<double>(Resource&, std::array<double*,10>, std::array<double,10>, int, const std::string&, std::function<void()>);

template AnimationInstance<int>* AnimationPool::addInstance<int>(Resource&, std::initializer_list<std::reference_wrapper<int>>, std::initializer_list<int>, const std::string&, std::function<void()>);
template AnimationInstance<float>* AnimationPool::addInstance<float>(Resource&, std::initializer_list<std::reference_wrapper<float>>, std::initializer_list<float>, const std::string&, std::function<void()>);
template AnimationInstance<double>* AnimationPool::addInstance<double>(Resource&, std::initializer_list<std::reference_wrapper<double>>, std::initializer_list<double>, const std::string&, std::function<void()>);

template AnimationInstance<int>* AnimationPool::addInstance<int>(Resource&, std::array<int*,10>, std::array<int,10>, int, const std::string&);
template AnimationInstance<float>* AnimationPool::addInstance<float>(Resource&, std::array<float*,10>, std::array<float,10>, int, const std::string&);
template AnimationInstance<double>* AnimationPool::addInstance<double>(Resource&, std::array<double*,10>, std::array<double,10>, int, const std::string&);

template AnimationInstance<int>* AnimationPool::addInstanceWithDef<int>(Resource&, std::array<int*,10>, std::array<int,10>, int, const AnimDef&, std::function<void()>);
template AnimationInstance<float>* AnimationPool::addInstanceWithDef<float>(Resource&, std::array<float*,10>, std::array<float,10>, int, const AnimDef&, std::function<void()>);
template AnimationInstance<double>* AnimationPool::addInstanceWithDef<double>(Resource&, std::array<double*,10>, std::array<double,10>, int, const AnimDef&, std::function<void()>);
