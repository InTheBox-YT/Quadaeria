#pragma once
#include <flecs.h>
#include <cassert>
#include <iterator>
#include <type_traits>
#include <vector>

namespace qc {
struct DroppedItem {};
struct Explosive {};
struct FallingVoxel {};
struct FlyingDebris {};

struct ImmediateWrites {
    flecs::world& world;
    bool suspended;
    explicit ImmediateWrites(flecs::world& w) : world(w), suspended(w.is_deferred()) {
        if (suspended) world.defer_suspend();
    }
    ~ImmediateWrites() {
        if (suspended) world.defer_resume();
    }
    ImmediateWrites(const ImmediateWrites&) = delete;
    ImmediateWrites& operator=(const ImmediateWrites&) = delete;
};

template <class T, class... Tags> class EcsTable {
public:
    void init(flecs::world& world) {
        world_ = world.c_ptr();
        auto b = world.query_builder<T>();
        (b.template with<Tags>(), ...);
        query_ = b.cache_kind(flecs::QueryCacheAuto).build();
    }
    struct End {};
    template <class V> class Iter {
    public:
        Iter(ecs_world_t* w, const ecs_query_t* q, int* busy) : busy_(busy) {
            ++*busy_;
            it_ = ecs_query_iter(w, q);
            live_ = true;
            nextTable();
        }
        ~Iter() {
            if (live_) ecs_iter_fini(&it_);
            --*busy_;
        }
        Iter(const Iter&) = delete;
        Iter& operator=(const Iter&) = delete;
        V& operator*() const { return col_[row_]; }
        V* operator->() const { return col_ + row_; }
        Iter& operator++() {
            if (++row_ >= it_.count) nextTable();
            return *this;
        }
        bool operator!=(End) const { return live_; }
        flecs::entity_t entity() const { return it_.entities[row_]; }

    private:
        void nextTable() {
            row_ = 0;
            while ((live_ = ecs_query_next(&it_))) {
                if (it_.count) {
                    col_ = static_cast<V*>(ecs_field_w_size(&it_, sizeof(T), 0));
                    return;
                }
            }
        }
        ecs_iter_t it_;
        V* col_ = nullptr;
        int32_t row_ = 0;
        bool live_ = false;
        int* busy_;
    };
    Iter<T> begin() { return Iter<T>(world_, query_.c_ptr(), &busy_); }
    Iter<const T> begin() const { return Iter<const T>(world_, query_.c_ptr(), &busy_); }
    End end() const { return {}; }
    int count() const { return query_.count(); }
    size_t size() const { return size_t(count()); }
    bool empty() const { return count() == 0; }
    bool busy() const { return busy_ > 0; }

private:
    ecs_world_t* world_ = nullptr;
    flecs::query<T> query_;
    mutable int busy_ = 0;
};

template <class T> class StableList {
    struct Entry {
        flecs::entity entity;
        T* value;
    };

public:
    explicit StableList(flecs::world& world) : world_(world) { world_.component<T>().add(flecs::Sparse); }
    StableList(const StableList&) = delete;
    StableList& operator=(const StableList&) = delete;

    template <bool Const> class Iter {
        using List = std::conditional_t<Const, const StableList, StableList>;
        List* list_;
        size_t index_;
        friend class StableList;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;
        Iter(List* l, size_t i) : list_(l), index_(i) {}
        reference operator*() const { return (*list_)[index_]; }
        pointer operator->() const { return &**this; }
        Iter& operator++() {
            ++index_;
            return *this;
        }
        bool operator==(const Iter& o) const { return index_ == o.index_; }
        bool operator!=(const Iter& o) const { return index_ != o.index_; }
    };
    auto begin() { return Iter<false>(this, 0); }
    auto end() { return Iter<false>(this, size()); }
    auto begin() const { return Iter<true>(this, 0); }
    auto end() const { return Iter<true>(this, size()); }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    T& operator[](size_t i) { return *entries_[i].value; }
    const T& operator[](size_t i) const { return *entries_[i].value; }
    T& front() { return (*this)[0]; }
    const T& front() const { return (*this)[0]; }
    flecs::entity_t handle(size_t i) const { return entries_[i].entity.id(); }
    flecs::entity_t handleOf(const T& value) const {
        for (const auto& e : entries_)
            if (e.value == &value) return e.entity.id();
        return 0;
    }
    template <class U> flecs::entity push_back(U&& value) {
        ImmediateWrites live(world_);
        auto e = world_.entity();
        e.set<T>(std::forward<U>(value));
        entries_.push_back({e, &e.template get_mut<T>()});
        return e;
    }
    auto erase(Iter<false> it) {
        size_t i = it.index_;
        entries_[i].entity.destruct();
        entries_.erase(entries_.begin() + long(i));
        return Iter<false>(this, i);
    }
    template <class Pred> void erase_if(Pred&& pred) {
        size_t dst = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (pred(*entries_[i].value))
                entries_[i].entity.destruct();
            else
                entries_[dst++] = entries_[i];
        }
        entries_.resize(dst);
    }
    void clear() {
        for (auto& e : entries_) e.entity.destruct();
        entries_.clear();
    }

private:
    flecs::world& world_;
    std::vector<Entry> entries_;
};

}
