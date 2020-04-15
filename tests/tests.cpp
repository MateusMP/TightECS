#include <set>

#include "catch2/catch.hpp"


#define TECS_CHECK(expr, message) INFO(message) REQUIRE(expr)
#define TECS_ASSERT(expr, message) INFO(message) REQUIRE(expr)

#include <tecs/tecs.h>

// Define some components
struct Component1
{
    int x;
};

struct Component2
{
    int x;
    int y;
};

struct Component3
{
    int x;
    int y;
    int z;
};

struct Component4
{
    float x;
};

struct Component5
{
    bool a;
};

CREATE_COMPONENT_TYPES(ComponentTypes);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component1, 1);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component2, 2);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component3, 3);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component4, 3);
REGISTER_COMPONENT_TYPE(ComponentTypes, Component5, 3);

#define MEGABYTES(number) 1024 * 1024 * number

using namespace tecs;

using EntitySystem = Ecs<ComponentTypes, 64>;

class MemoryReadyEcs : public EntitySystem {
public:
    MemoryReadyEcs(u32 memSize, u32 maxEntities)
    {
        memory = std::make_unique<char[]>(memSize);
        this->init(ArenaAllocator(memory.get(), memSize), maxEntities);
    }

    std::unique_ptr<char[]> memory;
};

TEST_CASE("Memory footprint", "[footprint]")
{
    // Kepp track of class footprint based on maximum allowed component types
    REQUIRE(sizeof(Ecs<ComponentTypes, 128>) == 6192);    // 6 KB
    REQUIRE(sizeof(Ecs<ComponentTypes, 64>) == 3120);     // 3 KB
    REQUIRE(sizeof(Ecs<ComponentTypes, 32>) == 1584);     // 1.5 KB
    REQUIRE(sizeof(Ecs<ComponentTypes, 16>) == 816);      // 0.8 KB
    REQUIRE(sizeof(Ecs<ComponentTypes, 8>) == 432);       // 0.4 KB
}


TEST_CASE("Create entity starts with no components", "[entity]")
{
    MemoryReadyEcs ecs(MEGABYTES(1), 2);

    EntityHandle entity = ecs.newEntity();
    REQUIRE(ecs.isEntityAlive(entity) == true);
    REQUIRE(entity.id == 1);
    for (int i = 0; i < ecs.MaxComponents; ++i) {
        REQUIRE(ecs.entityHasComponent(entity, i) == false);
    }

    entity = ecs.newEntity();
    REQUIRE(ecs.isEntityAlive(entity) == true);
    REQUIRE(entity.id == 2);
    for (int i = 0; i < ecs.MaxComponents; ++i) {
        REQUIRE(ecs.entityHasComponent(entity, i) == false);
    }
}

TEST_CASE("Create all entities possible", "[entity]")
{
    // Reuse same memory for all sections
    char* memory = new char[MEGABYTES(50)];

    SECTION("1000 entities") {
        EntitySystem ecs(tecs::ArenaAllocator(memory, MEGABYTES(1)), 1000);
        for (int i = 1; i <= 1000; ++i) {
            EntityHandle entity = ecs.newEntity();
            REQUIRE(entity.id == i);
        }
    }

    SECTION("10.000 entities") {
        EntitySystem ecs(tecs::ArenaAllocator(memory, MEGABYTES(50)), 10'000);
        for (int i = 1; i <= 10'000; ++i) {
            EntityHandle entity = ecs.newEntity();
            REQUIRE(entity.id == i);
        }
    }

    // Starts to get too big for stack, so use dynamic allocation instead
    SECTION("100.000 entities") {
        EntitySystem ecs(tecs::ArenaAllocator(memory, MEGABYTES(50)), 100'000);
        for (int i = 1; i <= 100'000; ++i) {
            EntityHandle entity = ecs.newEntity();
            REQUIRE(entity.id == i);
        }
    }

    delete[] memory;
}


TEST_CASE("Generation of new entities should invalidate references", "[entity]")
{
    char* memory = new char[MEGABYTES(1)];

    SECTION("check 1000 entities, every 3 are killed") {
        EntitySystem ecs(tecs::ArenaAllocator(memory, MEGABYTES(1)), 1000);
        EntityHandle handles[1000] = {};
        for (int i = 1; i <= 1000; ++i) {
            EntityHandle entity = ecs.newEntity();
            handles[i-1] = entity;
        }

        for (int i = 0; i < 1000; i+=1) {
            EntityHandle handle = handles[i];
            INFO("i: " << i << " Handle: " << handle);
            if (i % 3 == 0) {
                ecs.removeEntity(handle);
                REQUIRE(ecs.isEntityAlive(handle) == false);
                REQUIRE(ecs.isEntityHandleValid(handle) == false);
            } else { // Other ids should still be alive
                REQUIRE(ecs.isEntityAlive(handle) == true);
                REQUIRE(ecs.isEntityHandleValid(handle) == true);
            }
        }

        // Re-check all to make sure nothing was corrupted
        for (int i = 0; i < 1000; i+=1) {
            EntityHandle handle = handles[i];
            INFO("i: " << i << " Handle: " << handle);
            if (i % 3 == 0) {
                REQUIRE(ecs.isEntityAlive(handle) == false);
                REQUIRE(ecs.isEntityHandleValid(handle) == false);
            } else { // Other ids should be alive
                REQUIRE(ecs.isEntityAlive(handle) == true);
                REQUIRE(ecs.isEntityHandleValid(handle) == true);
            }
        }

    }

    delete[] memory;
}

TEST_CASE("Create entity starts with no components after reusing id", "[entity]")
{
    constexpr u32 memSize = 1024 * 16;
    char memory[memSize];
    EntitySystem ecs(ArenaAllocator(memory, memSize), 2);

    EntityHandle entity = ecs.newEntity();
    REQUIRE(ecs.isEntityAlive(entity) == true);
    REQUIRE(entity.id == 1);
    REQUIRE(entity.generation == 0);
    ecs.addComponent<Component1>(entity);
    REQUIRE(ecs.entityHasComponent<Component1>(entity));
    ecs.removeEntity(entity);
    
    entity = ecs.newEntity();
    REQUIRE(!ecs.entityHasComponent<Component1>(entity));
}


TEST_CASE("Create many entities with one component", "[entity component]")
{
    // Make sure our chunk and ids are used properly
    SECTION("Components should be unique for unique entities") {
        MemoryReadyEcs ecs(MEGABYTES(1), 1000);

        std::set<void*> seen;
        for (int i = 0; i < 1000; ++i) {
            EntityHandle e = ecs.newEntity();
            INFO("Entity Id: " << e.id << " Generation " << e.generation << " is alive: " << e.alive);

            Component1& c1 = ecs.addComponent<Component1>(e) = {1};

            // ensure we always get a unique addresses and valid handle
            REQUIRE(ecs.entityHasComponent<Component1>(e));
            REQUIRE(seen.insert(&c1).second == true);
        }
        REQUIRE(seen.size() == 1000);
    }
}


TEST_CASE("Loop entities with one component", "[entity loop]")
{
    MemoryReadyEcs ecs(MEGABYTES(1), 1000);

    for (int i = 0; i < 1000; ++i) {
        EntityHandle e = ecs.newEntity();
        ecs.addComponent<Component1>(e) = {i};
    }
    
    int sumX = 0;
    int timesCalled = 0;
    ecs.forEach<Component1>([&](EntityHandle e, Component1& c1){
        sumX += c1.x;
        ++timesCalled;
    });
    REQUIRE(timesCalled == 1000);
    // Sum of sequence, first is 0 so we have only 999 numbers!
    REQUIRE(sumX == 999*(999+1)/2);
}


TEST_CASE("Loop entities with two components", "[entity loop]")
{
    MemoryReadyEcs ecs(MEGABYTES(1), 1000);

    for (int i = 0; i < 1000; ++i) {
        EntityHandle e = ecs.newEntity();
        ecs.addComponent<Component1>(e) = {i};
        ecs.addComponent<Component2>(e) = {i, i*3};
    }
    
    int sumX = 0;
    int sumX2 = 0;
    int sumY = 0;
    int timesCalled = 0;
    ecs.forEach<Component1, Component2>([&](EntityHandle e, Component1& c1, Component2& c2){
        sumX += c1.x;
        sumX2 += c2.x;
        sumY += c2.y;
        ++timesCalled;
    });
    REQUIRE(timesCalled == 1000);
    // Sum of sequence, first is 0 so we have only 999 numbers!
    REQUIRE(sumX == 999*(999+1)/2);
    REQUIRE(sumX == sumX2);
    REQUIRE(sumY == sumX2*3);
}

TEST_CASE("Loop entities 1000 entities only one entity has 2 components", "[entity loop]")
{
    MemoryReadyEcs ecs(MEGABYTES(1), 1000);

    for (int i = 0; i < 1000; ++i) {
        EntityHandle e = ecs.newEntity();
        ecs.addComponent<Component1>(e) = {i};
        if (i == 500) {
            ecs.addComponent<Component2>(e) = {i, i * 3};
        }
    }

    SECTION("Check sum of entities with Component1")
    {
        int sumX = 0;
        int timesCalled = 0;
        ecs.forEach<Component1>([&](EntityHandle e, Component1& c1) {
            sumX += c1.x;
            ++timesCalled;
        });
        REQUIRE(timesCalled == 1000);
        // Sum of sequence, first is 0 so we have only 999 numbers!
        REQUIRE(sumX == 999 * (999 + 1) / 2);
    }


    SECTION("Check for just component 2 (only one instance has it)")
    {
        int sumX = 0;
        int sumY = 0;
        int sumX2 = 0;
        int timesCalled = 0;
        ecs.forEach<Component2>([&](EntityHandle e, Component2& c2) {
            sumX2 += c2.x;
            sumY += c2.y;
            ++timesCalled;
        });
        REQUIRE(timesCalled == 1);
        REQUIRE(sumX2 == 500);
        REQUIRE(sumY == 500 * 3);
    }

    SECTION("Check for just both components (only one instance has it)")
    {
        int sumX = 0;
        int sumY = 0;
        int sumX2 = 0;
        int timesCalled = 0;
        ecs.forEach<Component1, Component2>([&](EntityHandle e, Component1& c1,  Component2& c2) {
            sumX += c1.x;
            sumX2 += c2.x;
            sumY += c2.y;
            ++timesCalled;
        });
        REQUIRE(timesCalled == 1);
        // Sum of sequence, first is 0 so we have only 999 numbers!
        REQUIRE(sumX == 500);
        REQUIRE(sumX == sumX2);
        REQUIRE(sumY == sumX2 * 3);
    }
}


TEST_CASE("Destroyed entity must have it's components invalidated", "[entity components]")
{
    MemoryReadyEcs ecs(MEGABYTES(1), 1000);

    for (int i = 0; i < 1000; ++i) {
        EntityHandle e = ecs.newEntity();
        ecs.addComponent<Component1>(e) = {i};
        ecs.addComponent<Component2>(e) = {i, i * 3};
        REQUIRE(ecs.entityHasComponent<Component1>(e));
        REQUIRE(ecs.entityHasComponent<Component2>(e));
        ecs.removeEntity(e);
        REQUIRE(!ecs.entityHasComponent<Component1>(e));
        REQUIRE(!ecs.entityHasComponent<Component2>(e));
    }
}