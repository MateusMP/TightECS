# Tight Entity Component System

# About
This is (yet) another implementation of an entity component system (ECS).

However, this implementation differs in it's approach for memory allocation.
All memory allocation is performed through an ArenaAllocator, that must be provided
before using the ECS.

The implementation tries to keep components of the same type packed together
to be as cache friendly as possible.

# Build

Use cmake to build the project.

```
cmake -G "Unix Makefiles" -B build

cd build

make tests
make example1

./tests
./example1
```

# Example Usage

A simple example is available below (examples/example1.cpp).
Also, see tests files for other examples of usage and memory allocation.

```c++
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

int main() {
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

    ecs.forEach<Position, Velocity>([](tecs::EntityHandle e, Position& pos, Velocity& vel){
        pos.x += vel.x;
        pos.y += vel.y;
    });

    // Show entity positions:
    ecs.forEach<Position>([](tecs::EntityHandle e, Position& pos){
        std::cout << "Entity: " << e << ", Position: " << pos.x << ", " << pos.y << std::endl;
    });

    delete []memory;
}

```
### Output:
```
$ ./example1.exe
Entity: EntityHandle(alive:1 v:0 id:1), Position: 2, 2
Entity: EntityHandle(alive:1 v:0 id:2), Position: 3, 3
```

# License

Code distributed on MIT license @ Copyright 2020 Mateus Malvessi Pereira

If you use this library, please, let me know!

