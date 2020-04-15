#include <iostream>

#include <tecs/tecs.h>

struct Position {
    float x;
    float y;
};

struct Velocity {
    float x;
    float y;
};

CREATE_COMPONENT_TYPES(TypeProvider);
REGISTER_COMPONENT_TYPE(TypeProvider, Position, 1);
REGISTER_COMPONENT_TYPE(TypeProvider, Velocity, 2);

int main()
{
    // Feel free to allocate/manage the memory anyway you want.
    constexpr u32 MEMORY_SIZE = 1024 * 1024 * 8;
    char* memory = new char[MEMORY_SIZE]; // Allocate 8MB

    const u32 maxEntities = 1000;

    tecs::Ecs<TypeProvider, 8> ecs(tecs::ArenaAllocator(memory, MEMORY_SIZE), maxEntities);

    auto e = ecs.newEntity();
    ecs.addComponent<Position>(e) = {1, 1};
    ecs.addComponent<Velocity>(e) = {1, 1};

    e = ecs.newEntity();
    ecs.addComponent<Position>(e) = {1, 1};
    ecs.addComponent<Velocity>(e) = {2, 2};

    ecs.forEach<Position, Velocity>([](tecs::EntityHandle e, Position& pos, Velocity& vel) {
        pos.x += vel.x;
        pos.y += vel.y;
    });

    // Show entity positions:
    ecs.forEach<Position>([](tecs::EntityHandle e, Position& pos) {
        std::cout << "Entity: " << e << ", Position: " << pos.x << ", " << pos.y
                  << std::endl;
    });

    delete[] memory;
}