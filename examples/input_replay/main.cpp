// CPU-only example: fixed-step actions, scheduled simulation, named RNG and checked replay.
#include "Core/Error.h"
#include "Core/Random.h"
#include "Gameplay/SystemScheduler.h"
#include "Input/Input.h"
#include "Input/Recording.h"

#include <iostream>
#include <string>

namespace {
struct Simulation {
    zodar::Input input;
    zodar::Random random{1337};
    zodar::SystemScheduler systems;
    int position = 0;
    uint64_t pulse = 0;

    explicit Simulation(const zodar::InputMap& map) {
        input.setMap(map);
        systems.add({"move", zodar::GameplayPhase::FixedSimulation,
            [this](const zodar::SystemContext&) {
                position += static_cast<int>(input.action("move").x);
                if (input.action("pulse").pressed) pulse = random.next("pulse");
            }});
        systems.validate();
    }

    std::string step(uint64_t tick) {
        input.beginStep();
        systems.run(zodar::GameplayPhase::FixedSimulation, {1.0 / 60.0, tick, 0});
        std::string state = std::to_string(position) + ":" + std::to_string(pulse);
        for (const auto& [name, value] : random.states())
            state += ":" + name + ":" + std::to_string(value);
        return zodar::inputHash(state);
    }
};
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) zodar::fail("usage: ZodarInputReplay OUTPUT.inputrec.jsonl");
        const auto map = zodar::InputMap::load(ZODAR_INPUT_REPLAY_MAP);
        zodar::InputRecording recording;
        recording.header = {"ZodarInputReplay", map.hash(), zodar::inputHash("example-v1"), 1337, 1.0 / 60.0};
        Simulation original(map);
        for (uint64_t tick = 1; tick <= 60; ++tick) {
            original.input.feed("key", "D", tick <= 40 ? 1.0f : 0.0f);
            original.input.feed("key", "A", tick > 40 ? 1.0f : 0.0f);
            original.input.feed("key", "Space", tick == 10 || tick == 30 ? 1.0f : 0.0f);
            const auto state = original.step(tick);
            recording.ticks.push_back({original.input.snapshot(), state});
        }
        recording.save(argv[1]);
        const auto replay = zodar::InputRecording::load(argv[1]);
        replay.checkCompatible(recording.header);
        Simulation restored(map);
        uint64_t tick = 0;
        for (const auto& row : replay.ticks) {
            for (const auto& [name, action] : row.actions) restored.input.inject(name, action);
            if (restored.step(++tick) != row.state) zodar::fail("replay diverged at tick " + std::to_string(tick));
        }
        if (tick != 60 || restored.position != 20 || restored.pulse == 0 ||
            restored.random.states() != original.random.states()) zodar::fail("unexpected replay endpoint");
        std::cout << "60 replay ticks match; position 20; named RNG restored by replay\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
