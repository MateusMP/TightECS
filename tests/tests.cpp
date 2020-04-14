#include "catch2/catch.hpp"

#include "../tecs/tecs.h"

#include <set>

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

class ComponentTypes
{
public:
    template <typename T>
    static u32 TypeId();
};

#define ComponentType(Comp, id) \
    template <>                 \
    u32 ComponentTypes::TypeId<Comp>() { return id; }

ComponentType(Component1, 1);
ComponentType(Component2, 2);
ComponentType(Component3, 3);
ComponentType(Component4, 3);
ComponentType(Component5, 3);

using namespace tecs;

using EntitySystem = Ecs<ComponentTypes, Entity64, 1000>;

#define MEGABYTES(bytes) 0.000001 * (double)bytes

TEST_CASE("Memory footprint", "[footprint]")
{
    printf("Ecs<ComponentTypes, Entity64, 1.000>: %.3lf MB\n", MEGABYTES(sizeof(Ecs<ComponentTypes, Entity64, 1000>)));
    printf("Ecs<ComponentTypes, Entity64, 10.000>: %.3lf MB\n", MEGABYTES(sizeof(Ecs<ComponentTypes, Entity64, 10000>)));
    printf("Ecs<ComponentTypes, Entity64, 100.000>: %.3lf MB\n", MEGABYTES(sizeof(Ecs<ComponentTypes, Entity64, 100000>)));
    printf("Ecs<ComponentTypes, Entity64, 1.000.000>: %.3lf MB\n", MEGABYTES(sizeof(Ecs<ComponentTypes, Entity64, 1000000>)));

    // TODO: Implement entity chunks to reduce usage and allow unlimited entities
    REQUIRE(sizeof(Ecs<ComponentTypes, Entity64, 1000>)    ==    278184); // 0.27 MB
    REQUIRE(sizeof(Ecs<ComponentTypes, Entity64, 10000>)   ==   2618184); // 2.6 MB
    REQUIRE(sizeof(Ecs<ComponentTypes, Entity64, 100000>)  ==  26018184); // 26 MB
    REQUIRE(sizeof(Ecs<ComponentTypes, Entity64, 1000000>) == 260018184); // 260 MB

    printf("1 << 31 %lu\n", (u32)1 << 31);
    printf("1 << 61 %llu\n", ((long long)1) << 63);
}


TEST_CASE("Create entity starts with no components", "[entity]")
{
    Ecs<ComponentTypes, Entity64, 1000> ecs;

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
    SECTION("1000 entities") {
        Ecs<ComponentTypes, Entity64, 1000> ecs;
        for (int i = 1; i <= ecs.MaxEntities; ++i) {
            EntityHandle entity = ecs.newEntity();
            REQUIRE(entity.id == i);
        }
    }

    SECTION("10000 entities") {
        auto *ecs = new Ecs<ComponentTypes, Entity64, 10000>();
        for (int i = 1; i <= ecs->MaxEntities; ++i) {
            EntityHandle entity = ecs->newEntity();
            REQUIRE(entity.id == i);
        }
        delete ecs;
    }

    // Starts to get too big for stack, so use dynamic allocation instead
    SECTION("100000 entities") {
        auto *ecs = new Ecs<ComponentTypes, Entity64, 100000>();
        for (int i = 1; i <= ecs->MaxEntities; ++i) {
            EntityHandle entity = ecs->newEntity();
            REQUIRE(entity.id == i);
        }
        delete ecs;
    }
}


TEST_CASE("Generation of new entities should invalidate references", "[entity]")
{
    SECTION("check 1000 entities, every 3 are killed") {
        Ecs<ComponentTypes, Entity64, 1000> ecs;
        EntityHandle handles[1000] = {};
        for (int i = 1; i <= ecs.MaxEntities; ++i) {
            EntityHandle entity = ecs.newEntity();
            REQUIRE(entity.id == i);
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
}

TEST_CASE("Create entity starts with no components after reusing id", "[entity]")
{
    Ecs<ComponentTypes, Entity64, 1000> ecs;

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
        Ecs<ComponentTypes, Entity64, 1000> ecs;

        std::set<void*> seen;
        std::set<ComponentHandle> handles;
        for (int i = 0; i < 1000; ++i) {
            EntityHandle e = ecs.newEntity();
            INFO("Entity Id: " << e.id << " Generation " << e.generation << " is alive: " << e.alive);

            Component1& c1 = ecs.addComponent<Component1>(e) = {1};
            ComponentHandle componentHandle = ecs.getEntityComponentHandle<Component1>(e);
            INFO("Component Handle: " << componentHandle);
            
            // ensure we always get a unique addresses and valid handle
            REQUIRE(ecs.isComponentHandleValid(componentHandle));
            REQUIRE(seen.insert(&c1).second == true);
            REQUIRE(handles.insert(componentHandle).second == true);
        }
        REQUIRE(seen.size() == 1000);
    }
}


TEST_CASE("Loop entities with one component", "[entity loop]")
{
    Ecs<ComponentTypes, Entity64, 1000> ecs;

    for (int i = 0; i < 1000; ++i) {
        EntityHandle e = ecs.newEntity();
        ecs.addComponent<Component1>(e) = {i};
    }
    
    int sumX = 0;
    int timesCalled = 0;
    ecs.forEach<Component1>([&](Entity64& e, Component1* c1){
        sumX += c1->x;
        ++timesCalled;
    });
    REQUIRE(timesCalled == 1000);
    // Sum of sequence, first is 0 so we have only 999 numbers!
    REQUIRE(sumX == 999*(999+1)/2);
}

