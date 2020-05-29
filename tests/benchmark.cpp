#include <iostream>
#include <chrono>
#include <string_view>

#include "catch2/catch.hpp"

#define TECS_LOG_ERROR(message) std::cout << message << std::endl;
#define TECS_ASSERT(expr, message) REQUIRE(expr)

#include <tecs/tecs.h>

#define MEGABYTES(bytes) 1024 * 1024 * (double)bytes

class Timer {
    typedef std::chrono::time_point<std::chrono::high_resolution_clock> TimePoint;

public:
    Timer()
    {
        start();
    }

    void start()
    {
        startTime = std::chrono::high_resolution_clock::now();
    }

    /***
     * @return Time in seconds
     * */
    void stop(std::string_view message)
    {
        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diffSec = finish - startTime;
        std::cout << message << " took: " << diffSec.count() << " seconds" << std::endl;
    }

private:
    TimePoint startTime;
};

struct Component1 {
    long x;
};

struct Component2 {
    long x;
    long y;
};

CREATE_COMPONENT_TYPES(ComponentTypes);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component1, 1);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component2, 2);

class MemoryReadyEcs : public tecs::Ecs<ComponentTypes, 8> {
public:
    MemoryReadyEcs(u32 memSize, u32 maxEntities)
    {
        memory = std::make_unique<char[]>(memSize);
        this->init(tecs::ArenaAllocator(memory.get(), memSize), maxEntities);
    }

    std::unique_ptr<char[]> memory;
};

TEST_CASE("Create many entities", "[Benchmark]")
{
    const auto entitiesCount = 100'000;
    MemoryReadyEcs ecs(MEGABYTES(10), entitiesCount);

    Timer timer;
    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
    }
    timer.stop("Create 100.000 entities");
}

TEST_CASE("Create many entities with 2 components", "[Benchmark]")
{
    const auto entitiesCount = 100'000;
    MemoryReadyEcs ecs(MEGABYTES(10), entitiesCount);

    Timer timer;
    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        ecs.addComponent<Component1>(entity) = {i};
        ecs.addComponent<Component2>(entity) = {i, i};
    }
    timer.stop("Create 100.000 entities with 2 components");
}

TEST_CASE("Iterate over many entities with 2 components", "[Benchmark]")
{
    const auto entitiesCount = 100'000;
    MemoryReadyEcs ecs(MEGABYTES(10), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        ecs.addComponent<Component1>(entity) = {i};
        ecs.addComponent<Component2>(entity) = {i, i};
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Create 100.000 entities with 2 components");
}

TEST_CASE("Iterate over many entities with 2 components, sparse", "[Benchmark]")
{
    const auto entitiesCount = 100'000;
    MemoryReadyEcs ecs(MEGABYTES(10), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        ecs.addComponent<Component1>(entity) = {i};
        if (i > 1000 && i < 5000 || (i > 20'000 && i < 40'000) || i > 80'000) {
            ecs.addComponent<Component2>(entity) = {i, i};
        }
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Create 100.000 entities with 2 components sparse");
}

TEST_CASE("Iterate over many entities with 2 components some missing",
          "[Benchmark]")
{
    const auto entitiesCount = 100'000;
    MemoryReadyEcs ecs(MEGABYTES(10), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        if ((i % 7) != 0) {
            ecs.addComponent<Component1>(entity) = {i};
        }
        if ((i % 13) != 0) {
            ecs.addComponent<Component2>(entity) = {i, i};
        }
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Create 100.000 entities with 2 components some missing");
}


TEST_CASE("Iterate over 1M entities with 2 components",
          "[Benchmark]")
{
    const auto entitiesCount = 1'000'000;
    MemoryReadyEcs ecs(MEGABYTES(32), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        ecs.addComponent<Component1>(entity) = {i};
        ecs.addComponent<Component2>(entity) = {i, i};
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Iterate over 1M with 2 components");
}

TEST_CASE("Iterate over 1M entities with 2 components, some missing",
          "[Benchmark]")
{
    const auto entitiesCount = 1'000'000;
    MemoryReadyEcs ecs(MEGABYTES(30), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        if ((i % 7) != 0) {
            ecs.addComponent<Component1>(entity) = {i};
        }
        if ((i % 13) != 0) {
            ecs.addComponent<Component2>(entity) = {i, i};
        }
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Iterate over 1M with 2 components, some missing");
}

TEST_CASE("Iterate over 1M entities with 2 components, half contain components",
          "[Benchmark]")
{
    const auto entitiesCount = 1'000'000;
    MemoryReadyEcs ecs(MEGABYTES(28), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        if ((i % 2) != 0) {
            ecs.addComponent<Component1>(entity) = {i};
        }
        ecs.addComponent<Component2>(entity) = {i, i};
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Iterate over 1M with 2 components, half contain components");
}

TEST_CASE("Iterate over 1M entities with 2 components, less than half",
          "[Benchmark]")
{
    const auto entitiesCount = 1'000'000;
    MemoryReadyEcs ecs(MEGABYTES(28), entitiesCount);

    for (long i = 0; i < entitiesCount; ++i) {
        tecs::EntityHandle entity = ecs.newEntity();
        if ((i % 2) != 0) {
            ecs.addComponent<Component1>(entity) = {i};
        }
        if ((i % 3) != 0) {
            ecs.addComponent<Component2>(entity) = {i, i};
        }
    }

    Timer timer;
    ecs.forEach<Component1, Component2>([](auto, Component1& c1, Component2& c2) {
        c1.x = 0;
        c2.x = 1;
        c2.y = 2;
    });
    timer.stop("Iterate over 1M with 2 components, less than half");
}