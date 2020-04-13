#include "catch2/catch.hpp"

#include "../tecs/tecs.h"

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

TEST_CASE("Create entity starts with no components", "[entity]")
{
    Ecs<ComponentTypes, Entity64, 1000> ecs;

    Entity64* entity = ecs.newEntity();
    REQUIRE(entity != nullptr);
    REQUIRE(entity->id == 1);
    for (int i = 0; i < ecs.MaxComponents; ++i) {
        REQUIRE(entity->components[i] == 0);
    }

    entity = ecs.newEntity();
    REQUIRE(entity != nullptr);
    REQUIRE(entity->id == 2);
    for (int i = 0; i < ecs.MaxComponents; ++i) {
        REQUIRE(entity->components[i] == 0);
    }
}

TEST_CASE("Create all entities possible", "[entity]")
{
    SECTION("1000 entities") {
        Ecs<ComponentTypes, Entity64, 1000> ecs;
        for (int i = 1; i <= ecs.MaxEntities; ++i) {
            Entity64* entity = ecs.newEntity();
            REQUIRE(entity->id == i);
        }
    }

    SECTION("10000 entities") {
        auto *ecs = new Ecs<ComponentTypes, Entity64, 10000>();
        for (int i = 1; i <= ecs->MaxEntities; ++i) {
            Entity64* entity = ecs->newEntity();
            REQUIRE(entity->id == i);
        }
        delete ecs;
    }

    // Starts to get too big for stack, so use dynamic allocation instead
    SECTION("100000 entities") {
        auto *ecs = new Ecs<ComponentTypes, Entity64, 100000>();
        for (int i = 1; i <= ecs->MaxEntities; ++i) {
            Entity64* entity = ecs->newEntity();
            REQUIRE(entity->id == i);
        }
        delete ecs;
    }
}

TEST_CASE("Create entity starts with no components after reusing id", "[entity]")
{
    Ecs<ComponentTypes, Entity64, 1000> ecs;

    Entity64* entity = ecs.newEntity();
    REQUIRE(entity != nullptr);
    REQUIRE(entity->id == 1);
    ecs.addComponent<Component1>(entity);
    REQUIRE(ecs.componentHandleIsValid(entity->components[ComponentTypes::TypeId<Component1>()]));
    ecs.removeEntity(entity);
    
    entity = ecs.newEntity();
    REQUIRE(!ecs.componentHandleIsValid(entity->components[ComponentTypes::TypeId<Component1>()]));
}