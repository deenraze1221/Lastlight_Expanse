#include <doctest/doctest.h>

#include "Renderer/Resources/DeletionQueue.h"

#include <vector>

using zodar::gfx::DeletionQueue;

TEST_CASE("deletion queue: items wait for their submission to complete") {
    DeletionQueue q;
    std::vector<int> destroyed;
    q.retire([&] { destroyed.push_back(1); });
    CHECK(q.collect(100) == 0); // Not bound to a submission yet.
    q.onSubmit(5, true, false);
    q.retire([&] { destroyed.push_back(2); });
    q.onSubmit(6, true, false);
    CHECK(q.collect(4) == 0);
    CHECK(q.collect(5) == 1);
    CHECK(destroyed == std::vector<int>{1});
    CHECK(q.collect(10) == 1);
    CHECK(destroyed == std::vector<int>{1, 2});
}

TEST_CASE("deletion queue: uploads during frame recording do not adopt pending items") {
    DeletionQueue q;
    int destroyed = 0;
    q.retire([&] { ++destroyed; });
    q.onSubmit(7, false, true); // Upload while a frame is being recorded.
    q.collect(7);
    CHECK(destroyed == 0);
    CHECK(q.pendingCount() == 1);
    q.onSubmit(8, true, true);  // The frame itself.
    q.collect(8);
    CHECK(destroyed == 1);

    q.retire([&] { ++destroyed; });
    q.onSubmit(9, false, false); // Upload outside frame recording.
    q.collect(9);
    CHECK(destroyed == 2);
}

TEST_CASE("deletion queue: flushAll and destructor run everything") {
    int destroyed = 0;
    {
        DeletionQueue q;
        q.retire([&] { ++destroyed; });
        q.onSubmit(1, true, false);
        q.retire([&] { ++destroyed; });
    }
    CHECK(destroyed == 2);
}
