#include "Gameplay/Gameplay.h"
#include "Core/Error.h"
#include "Core/Profiler.h"
#include "Runtime/FixedTimestep.h"
#include <doctest/doctest.h>
#include <limits>
#include <string>
#include <vector>
using namespace zodar;

TEST_CASE("gameplay: stable topological ordering, forward names and phases") {
    SystemScheduler s;
    std::vector<std::string> order;
    const auto add = [&](std::string name, std::vector<std::string> before = {}, std::vector<std::string> after = {}) {
        const auto copy = name;
        s.add({std::move(name), GameplayPhase::FixedSimulation, [&, copy](const SystemContext&) { order.push_back(copy); }, std::move(before), std::move(after)});
    };
    add("first", {}, {"third"}); add("second"); add("third"); add("fourth", {}, {"first"});
    s.add({"input", GameplayPhase::Input, [&](const SystemContext&) { order.push_back("input"); }, {"second"}});
    s.validate();
    s.run(GameplayPhase::Input, {}); s.run(GameplayPhase::FixedSimulation, {});
    CHECK(order == std::vector<std::string>{"input", "second", "third", "first", "fourth"});
    CHECK_THROWS_AS(s.add({"later", GameplayPhase::Input, [](const auto&) {}}), Error);
}
TEST_CASE("gameplay: invalid dependency graphs fail before any callback") {
    int calls = 0;
    auto callback = [&](const SystemContext&) { ++calls; };
    SUBCASE("unknown") { SystemScheduler s; s.add({"a", GameplayPhase::Input, callback, {}, {"missing"}}); CHECK_THROWS_AS(s.run(GameplayPhase::Input, {}), Error); }
    SUBCASE("cycle") { SystemScheduler s; s.add({"a", GameplayPhase::Input, callback, {"b"}}); s.add({"b", GameplayPhase::Input, callback, {"a"}}); CHECK_THROWS_AS(s.validate(), Error); }
    SUBCASE("self") { SystemScheduler s; s.add({"a", GameplayPhase::Input, callback, {"a"}}); CHECK_THROWS_AS(s.validate(), Error); }
    SUBCASE("backwards phase") { SystemScheduler s; s.add({"a", GameplayPhase::Input, callback, {}, {"late"}}); s.add({"late", GameplayPhase::LateUpdate, callback}); CHECK_THROWS_AS(s.validate(), Error); }
    SUBCASE("duplicate") { SystemScheduler s; s.add({"a", GameplayPhase::Input, callback}); CHECK_THROWS_AS(s.add({"a", GameplayPhase::Input, callback}), Error); }
    SUBCASE("empty") { SystemScheduler s; CHECK_THROWS_AS(s.add({"", GameplayPhase::Input, callback}), Error); CHECK_THROWS_AS(s.add({"a", GameplayPhase::Input, {}}), Error); }
    CHECK(calls == 0);
}
TEST_CASE("gameplay: duplicate edges do not change indegree or ties") {
    SystemScheduler s; std::vector<int> trace;
    s.add({"a", GameplayPhase::Animation, [&](const auto&) { trace.push_back(1); }, {"b", "b"}});
    s.add({"b", GameplayPhase::Animation, [&](const auto&) { trace.push_back(2); }, {}, {"a"}});
    s.run(GameplayPhase::Animation, {});
    CHECK(trace == std::vector<int>{1, 2});
}
TEST_CASE("gameplay: typed events are next-tick, ordered and expire") {
    EventBus bus;
    struct A { int value; }; struct B { std::string value; };
    bus.emit(A{1}); bus.emit(A{2}); bus.emit(B{"other"});
    CHECK(bus.read<A>().empty()); bus.advance();
    const auto batch = bus.read<A>(); REQUIRE(batch.size() == 2);
    CHECK(batch[0].value == 1); CHECK(batch[1].value == 2); CHECK(bus.read<B>()[0].value == "other");
    bus.emit(A{3}); CHECK(batch[0].value == 1); CHECK(bus.read<A>().size() == 2);
    bus.advance(); REQUIRE(bus.read<A>().size() == 1); CHECK(bus.read<A>()[0].value == 3); CHECK(bus.read<B>().empty());
    bus.advance(); CHECK(bus.read<A>().empty());
}
TEST_CASE("gameplay: timer deadlines, stable ties, repeat and cancellation") {
    Timers timers; std::vector<int> trace;
    auto repeat = timers.after(2, [&] { trace.push_back(2); }, 2);
    timers.after(1, [&] { trace.push_back(1); });
    timers.after(2, [&] { trace.push_back(3); });
    timers.advance(); CHECK(trace == std::vector<int>{1});
    timers.advance(); CHECK(trace == std::vector<int>{1, 2, 3});
    timers.advance(); timers.advance(); CHECK(trace == std::vector<int>{1, 2, 3, 2});
    CHECK(timers.cancel(repeat)); CHECK_FALSE(timers.cancel(repeat));
    timers.advance(); timers.advance(); CHECK(trace.size() == 4); CHECK(timers.pending() == 0);
    CHECK_THROWS_AS(timers.after(0, [] {}), Error); CHECK_THROWS_AS(timers.after(1, {}), Error);
    CHECK_THROWS_AS(timers.after(std::numeric_limits<uint64_t>::max(), [] {}), Error);
}
TEST_CASE("gameplay: timer callbacks may cancel and schedule without same-tick recursion") {
    Timers t; std::vector<int> trace; Timers::Id other = 0, self = 0;
    self = t.after(1, [&] {
        trace.push_back(1); CHECK(t.cancel(self)); CHECK(t.cancel(other));
        t.after(1, [&] { trace.push_back(3); });
    }, 1);
    other = t.after(1, [&] { trace.push_back(2); });
    t.advance(); CHECK(trace == std::vector<int>{1});
    t.advance(); CHECK(trace == std::vector<int>{1, 3});
    CHECK(t.pending() == 0);
}
TEST_CASE("gameplay: end-tick boundary includes hooks, timers and published events") {
    Gameplay g; std::vector<std::string> trace;
    struct Event { uint64_t tick; };
    g.systems().add({"input", GameplayPhase::Input, [&](const auto& c) {
        trace.push_back("input"); CHECK(c.tick == g.completedTicks() + 1);
        if (c.tick == 1) { CHECK(g.events().read<Event>().empty()); g.events().emit(Event{1});
            g.timers().after(1, [&] { trace.push_back("nested timer"); g.events().emit(Event{2}); }); }
        if (c.tick == 2) { REQUIRE(g.events().read<Event>().size() == 2); CHECK(g.events().read<Event>()[0].tick == 1); }
    }});
    g.systems().add({"simulation", GameplayPhase::FixedSimulation, [&](const auto&) { trace.push_back("simulation"); }});
    g.systems().add({"animation", GameplayPhase::Animation, [&](const auto&) { trace.push_back("animation"); }});
    g.timers().after(1, [&] { trace.push_back("timer"); g.events().emit(Event{10}); });
    g.step(0.02, [&] { trace.push_back("hook"); CHECK(g.events().read<Event>().empty()); });
    CHECK(trace == std::vector<std::string>{"input", "simulation", "animation", "hook", "timer"});
    CHECK(g.completedTicks() == 1); REQUIRE(g.events().read<Event>().size() == 2);
    trace.clear(); g.step(0.02);
    CHECK(trace.back() == "nested timer"); CHECK(g.completedTicks() == 2);
    REQUIRE(g.events().read<Event>().size() == 1); CHECK(g.events().read<Event>()[0].tick == 2);
    g.step(0.02); CHECK(g.events().read<Event>().empty());
}
TEST_CASE("gameplay: multi-step frames match individual ticks and frame phases do not age state") {
    Gameplay g; FixedTimestep clock; std::vector<uint64_t> ticks;
    g.systems().add({"tick", GameplayPhase::FixedSimulation, [&](const auto& c) { ticks.push_back(c.tick); }});
    int fired = 0; g.timers().after(2, [&] { ++fired; }, 2);
    for (uint32_t i = 0, n = clock.advance(3.0/60.0); i < n; ++i) g.step(clock.fixedDt());
    CHECK(ticks == std::vector<uint64_t>{1, 2, 3}); CHECK(fired == 1);
    g.systems().run(GameplayPhase::LateUpdate, {0, g.completedTicks(), 0});
    g.systems().run(GameplayPhase::RenderSync, {0, g.completedTicks(), 0});
    CHECK(g.completedTicks() == 3); CHECK(g.timers().tick() == 3); CHECK(fired == 1);
    g.step(clock.fixedDt()); CHECK(fired == 2);
}
TEST_CASE("gameplay: profiling scopes close on exceptions and recursive execution is rejected") {
    SystemScheduler s; CpuProfiler profiler; profiler.beginFrame();
    s.add({"recursive", GameplayPhase::Input, [&](const auto&) { s.run(GameplayPhase::Input, {}); }});
    CHECK_THROWS_AS(s.run(GameplayPhase::Input, {}, &profiler), Error);
    profiler.endFrame(); REQUIRE(profiler.lastFrame().size() == 1);
    CHECK(profiler.lastFrame()[0].name == "System/recursive");
    Gameplay g; g.timers().after(1, [] { fail("timer failure"); });
    CHECK_THROWS_AS(g.step(0.02), Error); CHECK(g.completedTicks() == 0);
    CHECK_THROWS_AS(g.step(0.02), Error);
}

TEST_CASE("gameplay: repeating timers retain mutable callback state through self-cancellation") {
    Timers timers;
    std::vector<unsigned> calls;
    Timers::Id self = 0;
    self = timers.after(1, [count = 0u, &timers, &calls, &self]() mutable {
        ++count;
        if (count == 3) CHECK(timers.cancel(self));
        calls.push_back(count); // callable must remain alive after cancelling itself
    }, 1);
    for (int i = 0; i < 5; ++i) timers.advance();
    CHECK(calls == std::vector<unsigned>{1, 2, 3});
    CHECK(timers.pending() == 0);
}
