# Tight Entity Component System

# About
This is (yet) another implementation of an entity component system (ECS).

However, this implementation differs in it's approach for memory allocation.
All memory allocation is performed through an ArenaAllocator, that must be provided
before using the ECS.

The implementation tries to keep components of the same type packed together
to be as cache friendly as possible.

# Interesting Features
- All allocations are constrained to a *tight* memory arena. Want to clear everything? just drop the arena and initiate the ECS again. This also helps in keeping your ECS working across boundaries.
- Component references are guaranteed to be valid, independently if you add or remove more entities. Of course, if the entity or the component is removed, that reference no longer makes sense (you can still write data to it, but it might affect other entities) or components.
- Components are *tight*ly packed in memory (as much as possible) in order to be cache-friendly when iterating over them.
- Compile time type-safe API. Run-time types are planned to be supported as well.

# Limitations
- You must be able to provide unique sequential identifiers for your components. They don't need to be necessarely sequential, but must be small numbers to guarantee best memory utilization.
- You are limited to the memory allocated to the ECS in the beginning, so you have a big cost upfront, however, you should have no issues with memory allocation after that (unless you try to create too many entities/components).


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
    // Feel free to allocate/manage the memory any way you want. 
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

### Benchmark:

See [tests/benchmark.cpp](https://github.com/MateusMP/TightECS/tree/master/tests/benchmark.cpp) for implementation details.
```
Create 100.000 entities took: 0.002992 seconds
Create 100.000 entities with 2 components took: 0.0149592 seconds
Create 100.000 entities with 2 components took: 0.0009973 seconds
Create 100.000 entities with 2 components sparse took: 0.0009974 seconds
Create 100.000 entities with 2 components some missing took: 0.000997 seconds
Iterate over 1M with 2 components took: 0.0139626 seconds
Iterate over 1M with 2 components, some missing took: 0.0119679 seconds
Iterate over 1M with 2 components, half contain components took: 0.0063857 seconds
Iterate over 1M with 2 components, less than half took: 0.0059839 seconds
```
CPU: i7-9750H

Note: The results may vary depending on computer configuration!


# License

Code distributed on MIT license @ Copyright 2020 Mateus Malvessi Pereira

If you use this library, please, let me know!

