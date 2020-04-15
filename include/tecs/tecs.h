#ifndef _TECS_H_
#define _TECS_H_

#include <cstring>
#include <array>
#include <assert.h>
#include <cassert>

#ifndef SKIP_DEFINE_OSTREAM_SERIALIZATION
#include <ostream>
#endif

typedef unsigned long u32;

// Some macros used for error logging
// Do nothing by default, define macros to use your own assert/check/logs.
#ifndef TECS_LOG_ERROR
#define TECS_LOG_ERROR(message) ((void)0);
#endif

#ifndef TECS_ASSERT
#define TECS_ASSERT(expression, message) ((void)0);
#endif

#ifndef TECS_CHECK
#define TECS_CHECK(expression, message) \
    if (expression) {                   \
        TECS_LOG_ERROR(message);        \
    }
#endif

template <typename T>
struct AlwaysFalse : std::false_type {
};

#define CREATE_COMPONENT_TYPES(ComponentTypes)                                 \
    class ComponentTypes {                                                     \
    public:                                                                    \
        template <typename T>                                                  \
        static u32 TypeId()                                                    \
        {                                                                      \
            static_assert(AlwaysFalse<T>::value, "Specialize this function!"); \
            return 0;                                                          \
        }                                                                      \
    }

#define REGISTER_COMPONENT_TYPE(ComponentTypes, CompClass, id) \
    template <>                                                \
    inline u32 ComponentTypes::TypeId<CompClass>()             \
    {                                                          \
        return id;                                             \
    }

namespace tecs {

/**
* Arena allocator used by ECS
* The ECS does not cat about freeing memory.
* All memory allocated by the arena can be freed by the caller
* once the ecs goes out of scope.
* The memory allocated is always recycled while the ecs is alive.
* The provided memory chunk is all that the ECS will ever use.
* Allocation of outbound memory will assert.
*/
struct ArenaAllocator {
    ArenaAllocator() : base{0}, total{0}, current{0}
    {
    }
    ArenaAllocator(char* memory, u32 size)
        : base{memory}, total{size}, current{base} {};

    /**
    * Allocs a chunk of memory from the arena.

    * @param size Amount in bytes to allocate.
    */
    template <typename T>
    T* alloc(u32 n)
    {
        const u32 size = sizeof(T) * n;
        TECS_ASSERT(current + size < base + total, "Arena overflow!");
        void* ptr = current;
        current += size;
        return (T*)ptr;
    }

private:
    char* base;
    u32 total;
    char* current;
};

typedef u32 ComponentHandle;

struct EntityHandleParts {
    u32 alive : 1;
    u32 generation : 3;
    u32 id : 28;
};

bool operator<(const EntityHandleParts& a, const EntityHandleParts& b)
{
    return (*(u32*)&a) < (*(u32*)&b);
}

#ifndef SKIP_DEFINE_OSTREAM_SERIALIZATION
inline std::ostream& operator<<(std::ostream& out, const EntityHandleParts& c)
{
    out << "EntityHandle(alive:" << c.alive << " v:" << c.generation
        << " id:" << c.id << ")";
    return out;
}
#endif

typedef EntityHandleParts EntityHandle;

struct ChunkEmptyEntry {
    u32 nextFree;
};

// TODO: Calculate this based on max entities and max component types
static constexpr u32 MaxComponentChunks = 32;

struct ComponentContainer {
    u32 idChunkSize = 256;
    u32 componentSize = 0;

    char** denseData; // char, but actually contains component data
    EntityHandle** denseEntities;
    u32 chunkSize; // Amount of entries in each dense chunk
    u32 aliveComponents = 0;
    ChunkEmptyEntry freeComponentHandle = {0};

    u32** sparseIds; // Indexes the component for each entity
};

/**
 *
 * @brief Entity Managing Class. Responsible for the creation and removal of
 * entities and components.
 * Requires a ArenaAllocator to perform memory allocations.
 * The Ecs will only use memory available in the ArenaAllocator for dynamic
 * allocations.
 * The memory in ArenaAllocator is never freed and always recycled.
 * Make sure to provide a big enough chunk of memory for your use case.
 *
 * @param TypeProvider A class that provides a static TypeId<T>() call to fetch
 * component type id.
 * component types must be sequential and begin from 1
 * @param MaxComponents_ Limit the amount of component types accepted.
 * Doesn't need to be an exact number, but must be AT LEAST the total types that
 * will be ever used. Base component container chunks are stored in the class
 * stack.
 *
**/
template <typename TypeProvider, unsigned char MaxComponents_>
class Ecs {
protected:
    struct TEntity {
        EntityHandle handle;
        // Maybe store something else?
    };

public:
    static constexpr auto MaxComponents = MaxComponents_;
    using Entity = TEntity;

    /**
    * @brief Default constructor provided for convinience.
    * Should be initialized by calling @see init()
    */
    Ecs()
    {
    }

    /**
    * @brief Contructor with arena allocator and max entities.
    * Max entities is required to allocate the space for containing
    * these entities up-front.
    * This avoids the need to check/create chunk for entities.
    *
    * @param arenaAllocator ArenaAllocator already initialized with memory
    * @param maxEntities Maximum number of entities that the Ecs is expected to
    * have
    *
    * TODO: Accept 0 as "unbounded" option.
    */
    Ecs(ArenaAllocator&& arenaAllocator, u32 maxEntities = 100'000)
    {
        init(std::move(arenaAllocator), maxEntities);
    }

    /**
    * @brief initializes the Ecs structure.
    *
    * @param arenaAllocator ArenaAllocator already initialized with memory
    * @param maxEntities Maximum number of entities that the Ecs is expected to
    * have
    */
    void init(ArenaAllocator&& arenaAllocator, u32 maxEntities)
    {
        this->maxEntities = maxEntities;
        allocator = arenaAllocator;
        entities = allocator.alloc<Entity>(maxEntities + 1); // 0 is reserved
        liveEntities = 0;
        containers = {};
        nextFreeEntity = 0;
    }

    /**
    * @brief Creates a new entity
    *
    * @return Returns a EntityHandle to be used for further operations @see
    * addComponent(), removeEntity()
    */
    EntityHandle newEntity()
    {
        u32 newId;
        if (nextFreeEntity > 0) {
            newId = nextFreeEntity;
            ++liveEntities;
            nextFreeEntity = ((ChunkEmptyEntry*)(&entities[nextFreeEntity]))->nextFree;
        }
        else {
            ++liveEntities;
            newId = liveEntities;
            TECS_ASSERT(newId <= maxEntities, "Can't create more entities!");
        }

        Entity& e = entities[newId];
        u32 id = newId;
        // TODO: Consider a bitfield variable for checking component handles
        // so we dont need to clean up the handles list
        EntityHandle prevHandle = e.handle;
        e = {};
        e.handle = e.handle;
        e.handle.id = id;
        e.handle.alive = 1;
        return e.handle;
    }

    /**
     * @brief removes an entity
     * Does nothing if the entity does not exist.
     * Associated components are also destroyed.
     *
     * @param handle The entity to be removed
     */
    void removeEntity(const EntityHandle entityHandle)
    {
        if (isEntityHandleValid(entityHandle)) {
            destroyExistingEntity(entityHandle);
        }
    }

    /**
     * @brief removes an entity
     * Assumes the entity is valid to be removed.
     * Use only if known that the entity exists.
     *
     * @param handle The entity to be removed
     */
    void destroyExistingEntity(const EntityHandle entityHandle)
    {
        Entity& e = entities[entityHandle.id];

        // TODO: Consider ways to reduce amount of containers we need to iterate
        // over
        u32 index = 0;
        for (ComponentContainer& container : containers) {
            removeComponentOfExistingEntity(entityHandle, index);
            ++index;
        }

        e.handle.id = nextFreeEntity; // ((ChunkEmptyEntry *)(&e))->nextFree =
                                      // nextFreeEntity;
        e.handle.generation += 1;
        e.handle.alive = 0;

        u32 prevId = entityHandle.id;
        nextFreeEntity = prevId;
        --liveEntities;
    }

    /**
     * @brief Check if an entity handle is valid
     *
     * @param handle the entity handle
     *
     * @return true if the entity is alive and is from the same generation.
     */
    bool isEntityHandleValid(EntityHandle handle)
    {
        return isEntityAlive(handle) &&
               entities[handle.id].handle.generation == handle.generation;
    }

    /**
     * @brief Add a component to an entity. The entity must exist!
     *
     * @param <T> the componen type
     * @param entityHandle the entity to add a component
     */
    template <typename T>
    T& addComponent(EntityHandle entityHandle)
    {
        static_assert(sizeof(T) >= sizeof(ChunkEmptyEntry));
        if (isEntityHandleValid(entityHandle)) {
            u32 compTypeId = TypeProvider::template TypeId<T>();

            ComponentContainer& c = ensureComponentContainer(compTypeId, sizeof(T));

            const u32 sparseEntityIdx = entityHandle.id / c.idChunkSize;
            const u32 denseEntityIdx = entityHandle.id % c.idChunkSize;
            if (c.sparseIds[sparseEntityIdx] == nullptr) {
                // This id was not in the set, so the entity does not have the
                // component.

                // Allocate sparse id chunk
                c.sparseIds[sparseEntityIdx] = allocator.alloc<u32>(c.idChunkSize);

                // Check if we can recycle any component handle
                if (isComponentHandleValid(c.freeComponentHandle.nextFree)) {
                    u32 componentHandle = c.freeComponentHandle.nextFree;
                    forwardFreeIndex(c);

                    c.sparseIds[sparseEntityIdx][denseEntityIdx] = componentHandle;
                    T* component = (T*)accessComponentData(c, componentHandle);
                    pushDenseEntity(c, entityHandle);
                    return *component;
                }
                else {
                    // Add next available dense index as the new component
                    u32 componentHandle = c.aliveComponents + 1;
                    c.sparseIds[sparseEntityIdx][denseEntityIdx] = componentHandle;
                    T* component = (T*)accessComponentData(c, componentHandle);
                    pushDenseEntity(c, entityHandle);
                    return *component;
                }
            }
            else {
                u32 possibleHandle = c.sparseIds[sparseEntityIdx][denseEntityIdx];
                if (isComponentHandleValid(possibleHandle)) {
                    // Entity already contains the component
                    return *(T*)accessComponentData(c, possibleHandle);
                }
                else {
                    // Add next available dense index as the new component
                    u32 componentHandle = c.aliveComponents + 1;
                    c.sparseIds[sparseEntityIdx][denseEntityIdx] = componentHandle;
                    T* component = (T*)accessComponentData(c, componentHandle);
                    pushDenseEntity(c, entityHandle);
                    return *component;
                }
            }
        }
        // TODO: Change this to use reserved space from component 0
        // This can be used to check if the user is using a bad component
        // doing some memory checking.
        // for now throws
        throw("Bad entity handle");
    }

    /**
     * @brief Get a component from an entity. 
     *
     * @param <T> the componen type
     * @param entityHandle the entity to add a component
     *
     * @return null if not found. Valid pointer otherwise.
     *
    */
    template <typename T>
    T* getComponent(EntityHandle entityHandle)
    {
        static_assert(sizeof(T) >= sizeof(ChunkEmptyEntry));
        if (isEntityHandleValid(entityHandle)) {
            u32 compTypeId = TypeProvider::template TypeId<T>();

            ComponentContainer& c = ensureComponentContainer(compTypeId, sizeof(T));

            const u32 sparseEntityIdx = entityHandle.id / c.idChunkSize;
            const u32 denseEntityIdx = entityHandle.id % c.idChunkSize;
            if (c.sparseIds[sparseEntityIdx] == nullptr) {
                return nullptr;
            }
            else {
                u32 possibleHandle = c.sparseIds[sparseEntityIdx][denseEntityIdx];
                if (isComponentHandleValid(possibleHandle)) {
                    // Entity already contains the component
                    return (T*)accessComponentData(c, possibleHandle);
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Access existing entity component's data
     * No checks are made, use only if you know the entity and component exists.
     * Otherwise, undefined behavior
     *
     * @param <T> The component type
     * @param entity the entity handle
     *
     * @return the pointer
     */
    template <typename T>
    T* accessExistingComponentData(u32 entity)
    {
        return (T*)accessExistingComponentData(TypeProvider::template TypeId<T>(), entity);
    }

    /**
     * @brief Access existing entity component's data
     * No checks are made, use only if you know the entity and component exists.
     * Otherwise, undefined behavior
     *
     * @param type The component type
     * @param entity the entity handle
     *
     * @return the pointer
     */
    void* accessExistingComponentData(u32 type, u32 entity)
    {
        ComponentContainer& c = containers[type];
        ComponentHandle component =
            c.sparseIds[entity / c.idChunkSize][entity % c.idChunkSize];
        return c.denseData[component / c.chunkSize] + (component % c.chunkSize) * c.componentSize;
    }

    /**
     * @brief Removes the component from a entity.
     * Does nothing if the entity is invalid.
     *
     * @param <T> component type
     * @param entity the entity
     */
    template <typename T>
    void removeComponent(EntityHandle entityHandle)
    {
        removeComponent(entityHandle, TypeProvider::template TypeId<T>());
    }

    /**
     * @brief Removes the component from a entity.
     * Does nothing if the entity is invalid.
     *
     * @param entity the entity
     * @param componentType component type id (from TypeProvider)
     */
    void removeComponent(EntityHandle entityHandle, u32 compTypeId)
    {
        if (isEntityHandleValid(entityHandle)) {
            removeComponentOfExistingEntity(entityHandle, compTypeId);
        }
        else {
            TECS_LOG_ERROR(
                "Trying to remove component of invalid entity handle! " << handle);
        }
    }

    /**
     * @brief Removes the component from an existing entity.
     * Slight optimized version of removeComponent(entity).
     * Use if you know that the entity is valid.
     *
     * @param entity the entity
     * @param componentType component type id (from TypeProvider)
     */
    void removeComponentOfExistingEntity(EntityHandle entityHandle, u32 componentType)
    {
        ComponentContainer& c = containers[componentType];
        if (c.componentSize == 0) {
            // Component not in use
            return;
        }

        const u32 sparseEntityIdx = entityHandle.id / c.idChunkSize;
        const u32 denseEntityIdx = entityHandle.id % c.idChunkSize;
        if (c.sparseIds[sparseEntityIdx] == nullptr) {
            // Entity didnt have the component
            return;
        }
        else {
            u32 possibleHandle = c.sparseIds[sparseEntityIdx][denseEntityIdx];
            if (isComponentHandleValid(possibleHandle)) {
                replaceDenseComponentFreeIndex(c, possibleHandle);
                c.sparseIds[sparseEntityIdx][denseEntityIdx] = 0;
                --c.aliveComponents;
            }
            else {
                // Entity didnt have the component
            }
        }
    }

    /**
     * @brief Check if the entity has a component
     *
     * @param entity the entity handle to be checked
     *
     * @return false if the entity is invalid or does not have the component,
     * true otherwise.
     **/
    template <typename T>
    bool entityHasComponent(EntityHandle entity)
    {
        return entityHasComponent(entity, TypeProvider::template TypeId<T>());
    }

    /**
     * @brief Check if the entity has a component
     *
     * @param entity the entity handle to be checked
     * @param componentType the component type (must be same as values provided
     * by TypeProvider template)
     *
     * @return false if the entity is invalid or does not have the component,
     * true otherwise.
     **/
    bool entityHasComponent(EntityHandle entity, u32 componentType)
    {
        if (isEntityHandleValid(entity)) {
            return getExistingEntityComponentHandle(entity.id, componentType) > 0;
        }
        return false;
    }

    /**
     * @brief Get the component handle for a given entity and component type
     *
     * @param <T> the component type
     * @param handle the entity
     *
     * @return the component handle. It may be invalid if the entity does not
     * exists
     * or if the entity does not have the component
     */
    template <typename T>
    ComponentHandle getEntityComponentHandle(EntityHandle handle)
    {
        return getEntityComponentHandle(handle, TypeProvider::template TypeId<T>());
    }

    /**
     * @brief Get the component handle for a given entity and component type
     *
     * @param handle the entity
     * @param componentType the component type
     *
     * @return the component handle. It may be invalid if the entity does not
     * exists
     * or if the entity does not have the component
     */
    ComponentHandle getEntityComponentHandle(EntityHandle handle, u32 componentType)
    {
        if (isEntityHandleValid(handle)) {
            return getExistingEntityComponentHandle(handle.id, componentType);
        }
        else {
            return 0;
        }
    }

    /**
     * @brief get component handle for a entity
     *
     * @param entity the entity
     * @param componentType the component type (from TypeProvider)
     *
     * Does not check is entity exists. If it doesnt, undefined behavior.
     * Only use thing method if you know what you are doing.
     */
    ComponentHandle getExistingEntityComponentHandle(u32 entity, u32 componentType)
    {
        ComponentContainer& c = containers[componentType];
        if (c.componentSize == 0) {
            return 0;
        }
        else {
            if (c.sparseIds[entity / c.idChunkSize] == nullptr) {
                return 0;
            }
            return c.sparseIds[entity / c.idChunkSize][entity % c.idChunkSize];
        }
    }

    /**
     * @brief Check if a entity is alive
     *
     * @param handle the entity to check.
     *
     * @return true if the entity is alive.
     */
    bool isEntityAlive(EntityHandle handle)
    {
        return entities[handle.id].handle.alive;
    }

    /**
     * @brief return the amount of currently active components of a given type
     *
     * @param type the component type to check
     *
     * @return the number of active components of a type
     */
    u32 getComponentAmount(u32 type)
    {
        if (containers[type].componentSize == 0) {
            return 0;
        }
        else {
            return containers[type].aliveComponents;
        }
    }

    /**
     * @brief Loops over all entities that contain a given set of components
     *
     * @param f a lambda function to be used.
     * Signature: (EntityHandle handle, Component1& c, Component2& ... etc)
     */
    template <typename... Components, typename F>
    void forEach(F f)
    {
        u32 compType[] = {TypeProvider::template TypeId<Components>()...};
        TypeAmount smallestType = findSmallestComponentContainer(
            TypeProvider::template TypeId<Components>()...);
        ComponentContainer& c = containers[smallestType.type];

        for (u32 i = 1; i <= smallestType.count; ++i) {
            u32 entity = c.denseEntities[i / c.chunkSize][i % c.chunkSize].id;
            if (entity > 0) {
                bool skip = false;
                for (int j = 0; j < sizeof...(Components); ++j) {
                    if (getExistingEntityComponentHandle(entity, compType[j]) == 0) {
                        skip = true;
                        break;
                    }
                }
                if (skip) {
                    continue;
                }

                Entity& e = entities[entity];
                f(e.handle, *accessExistingComponentData<Components>(entity)...);
            }
        }
    }

    template <typename T>
    u32 buildComponentMask(T first)
    {
        return first;
    }

    template <typename T, typename... Args>
    u32 buildComponentMask(T first, Args... args)
    {
        return first | buildComponentMask(args...);
    }

    template <typename... Args>
    u32 buildComponentMask()
    {
        return buildComponentMask(TypeProvider::template TypeId<Args>()...);
    }

    
private:
    /**
     * @brief Check if a given component handle is valid.
     * DOES NOT MEAN IT IS ATTACHED TO AN ENTITY.
     * Just that it is a valid handle id to be used internally.
     *
     * @param handle the component handle
     *
     * @return true if the component belongs to a live entity.
     */
    bool isComponentHandleValid(ComponentHandle handle)
    {
        return handle > 0 && handle <= componentsPerChunk * MaxComponentChunks;
    }

    /**
     * @brief Fetch a component address
     * No checks are made. Assumes component handle is valid.
     */
    template <typename T>
    T* getComponent(ComponentContainer& container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        u32 insideChunkIdx = (handle % componentsPerChunk) * container.componentSize;
        T* addr = (T*)(container.denseData[chunkIdx] + insideChunkIdx);
        return addr;
    }

    ComponentContainer& ensureComponentContainer(u32 typeId, u32 compSize)
    {
        TECS_ASSERT(
            compSize >= sizeof(ChunkEmptyEntry),
            "Compsize must be at least size of ChunkEmptyEntry (4 bytes)");
        ComponentContainer& c = containers[typeId];
        if (c.componentSize == 0) {
            c.componentSize = compSize;
            c.chunkSize = 1024 * 4 / c.componentSize; // TODO: define something
                                                      // that uses cache lines
                                                      // well
            // Alloc sparse ids array. Make sure to include all possible entries
            // with +1 for fraction
            u32 size = maxEntities / c.idChunkSize + 1;
            c.sparseIds = allocator.alloc<u32*>(size);
            c.denseData = allocator.alloc<char*>(MaxComponentChunks);
            c.denseEntities = allocator.alloc<EntityHandle*>(MaxComponentChunks);
            std::memset(c.sparseIds, 0, sizeof(u32*) * size);
            std::memset(c.denseData, 0, sizeof(char*) * MaxComponentChunks);
            std::memset(c.denseEntities, 0, sizeof(EntityHandle*) * MaxComponentChunks);
        }
        return c;
    }

    void* accessComponentData(tecs::ComponentContainer& c, const u32 componentHandle)
    {
        u32 compSparse = componentHandle / c.chunkSize;
        if (c.denseData[compSparse] == nullptr) {
            // Allocate dense data chunk
            const u32 chunkDataSize = c.componentSize * c.chunkSize;
            c.denseData[compSparse] = allocator.alloc<char>(chunkDataSize);
            c.denseEntities[compSparse] = allocator.alloc<EntityHandle>(c.chunkSize);
            return c.denseData[compSparse] + (componentHandle % c.chunkSize) * c.componentSize;
        }
        else {
            return c.denseData[compSparse] + (componentHandle % c.chunkSize) * c.componentSize;
        }
    }

    void pushDenseEntity(ComponentContainer& c, EntityHandle entity)
    {
        ++c.aliveComponents;
        c.denseEntities[c.aliveComponents / c.chunkSize][c.aliveComponents % c.chunkSize] = entity;
    }

    struct TypeAmount {
        u32 type;
        u32 count;
    };

    TypeAmount findSmallestComponentContainer(u32 type)
    {
        return {type, getComponentAmount(type)};
    }

    template <typename C, typename... Components>
    TypeAmount findSmallestComponentContainer(C type, Components... types)
    {
        auto a = findSmallestComponentContainer(type);
        auto b = findSmallestComponentContainer(types...);
        return (a.count < b.count) ? a : b;
    }

    /**
     * @brief Advances one in the linked list of free handles
     */
    void forwardFreeIndex(ComponentContainer& c)
    {
        const u32 sparseIdx = c.freeComponentHandle.nextFree / c.chunkSize;
        const u32 denseIdx = c.freeComponentHandle.nextFree % c.chunkSize;
        c.freeComponentHandle.nextFree =
            ((ChunkEmptyEntry*)(c.denseData[sparseIdx] + denseIdx))->nextFree;
    }

    // Save current nextFree at component location
    void replaceDenseComponentFreeIndex(ComponentContainer& c, u32 freeHandle)
    {
        const u32 sparseIdx = freeHandle / c.chunkSize;
        const u32 denseIdx = (freeHandle % c.chunkSize);

        // Replace destroyed entity index with last entry, so we don't have
        // holes in the dense entities vector
        // This then will point to the next valid data index
        c.denseEntities[sparseIdx][denseIdx] =
            c.denseEntities[c.aliveComponents / c.chunkSize][c.aliveComponents % c.chunkSize];
        c.denseEntities[c.aliveComponents / c.chunkSize][c.aliveComponents % c.chunkSize] = {};

        ((ChunkEmptyEntry*)(c.denseData[sparseIdx] + denseIdx * c.componentSize))->nextFree =
            c.freeComponentHandle.nextFree;
        c.freeComponentHandle.nextFree = freeHandle;
    }

protected:
    ArenaAllocator allocator;

    u32 nextFreeEntity;
    u32 liveEntities = 0;
    u32 maxEntities;
    static constexpr u32 componentsPerChunk = 128;
    Entity* entities = 0; // index 0 is reserved

    std::array<ComponentContainer, MaxComponents> containers;
};

} // namespace tecs

#endif