#ifndef _TECS_H_
#define _TECS_H_

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
#define TECS_CHECK(expression, message) if (expression){TECS_LOG_ERROR(message);}
#endif

template <typename T>
struct AlwaysFalse : std::false_type
{
};

#define CREATE_COMPONENT_TYPES(ComponentTypes) \
    class ComponentTypes { \
        public: \
        template <typename T> \
        static u32 TypeId(){static_assert(AlwaysFalse<T>::value, "Specialize this function!"); return 0;}}

#define REGISTER_COMPONENT_TYPE(ComponentTypes, CompClass, id) \
    template <>                 \
    inline u32 ComponentTypes::TypeId<CompClass>() { return id; }

namespace tecs
{

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
        : base{memory}, total{size}, current{base} {
    };

    /**
    * Allocs a chunk of memory from the arena.

    * @param size Amount in bytes to allocate.
    */
    template<typename T>
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

#ifndef SKIP_DEFINE_OSTREAM_SERIALIZATION
inline std::ostream & operator << (std::ostream &out, const EntityHandleParts &c) {
    out << "EntityHandle: " << c.alive << " " << c.generation << " " << c.id << std::endl;
    return out;
}
#endif
 

typedef EntityHandleParts EntityHandle;

struct ChunkEmptyEntry
{
    u32 nextFree;
};

struct ComponentChunk
{
    void *data;
};

static constexpr u32 MaxComponentChunks = 32;
struct ComponentContainer
{
    u32 componentSize = 0;
    u32 nextChunk = 0;
    ChunkEmptyEntry firstFreeEntry = {1};
    ComponentChunk *chunks[MaxComponentChunks] = {};
};

/**
* @brief Entity Managing Class. Responsible for the creation and removal of entities and components.
* Requires a ArenaAllocator to perform memory allocations.
* The Ecs will only use memory available in the ArenaAllocator for dynamic allocations.
* The memory in ArenaAllocator is never freed and always recycled.
* Make sure to provide a big enough chunk of memory for your use case.
*/
template <typename TypeProvider, unsigned char MaxComponents_>
class Ecs
{
protected:
    template <unsigned char MaxComps>
    struct TEntity
    {
        EntityHandle handle;
        u32 componentsMask;
        ComponentHandle components[MaxComponents_];
    };
public:
    static constexpr auto MaxComponents = MaxComponents_;
    using Entity = TEntity<MaxComponents>;

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
    * @param maxEntities Maximum number of entities that the Ecs is expected to have
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
    * @param maxEntities Maximum number of entities that the Ecs is expected to have
    */
    void init(ArenaAllocator&& arenaAllocator, u32 maxEntities)
    {
        this->maxEntities = maxEntities;
        allocator = arenaAllocator;
        entities = allocator.alloc<Entity>(maxEntities+1); // 0 is reserved
        liveEntities = 0;
        containers = {};
        nextFreeEntity = 0;
    }

    /**
    * @brief Creates a new entity
    *
    * @return Returns a EntityHandle to be used for further operations @see addComponent(), removeEntity()
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
        e.componentsMask = 0;
        e.handle.id = id;
        e.handle.alive = 1;
        return e.handle;
    }

    void removeEntity(const EntityHandle handle)
    {
        if (isEntityHandleValid(handle))
        {
            Entity& e = entities[handle.id];
            //((ChunkEmptyEntry *)(&e))->nextFree = nextFreeEntity;
            e.handle.id = nextFreeEntity;
            e.handle.generation += 1;
            e.handle.alive = 0;
            u32 prevId = handle.id;
            nextFreeEntity = prevId;
            --liveEntities;
        }
    }

    bool isComponentHandleValid(ComponentHandle handle)
    {
        return handle > 0 && handle < componentsPerChunk * MaxComponentChunks;
    }

    bool isEntityHandleValid(EntityHandle handle)
    {
        return isEntityAlive(handle) && entities[handle.id].handle.generation == handle.generation;
    }

    template <typename T>
    T &addComponent(EntityHandle entityHandle)
    {
        static_assert(sizeof(T) >= sizeof(ChunkEmptyEntry));
        if (isEntityHandleValid(entityHandle))
        {
            Entity& entity = entities[entityHandle.id];
            u32 compTypeId = TypeProvider::template TypeId<T>();
            if (isComponentHandleValid(entity.components[compTypeId]))
            {
                return *getComponent<T>(containers[compTypeId], entity.components[compTypeId]);
            }

            ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
            ComponentHandle handle = fetchNewComponentHandle(container);
            entity.components[compTypeId] = handle;
            entity.componentsMask |= compTypeId;
            return *getComponent<T>(container, handle);
        }
        throw ("Bad entity handle");
    }

    template <typename T>
    void removeComponent(EntityHandle entityHandle)
    {
        if (isEntityHandleValid(entityHandle)) {
            Entity &entity = entities[entityHandle.id];
            u32 compTypeId = TypeProvider::template TypeId<T>();
            ComponentHandle compHandle = entity.components[compTypeId];
            TECS_CHECK(isComponentHandleValid(compHandle), "Component handle is not valid!");
            entity.componentsMask &= ~(1UL << compTypeId);;

            ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
            getComponent<ChunkEmptyEntry>(container, compHandle).nextFree = container.firstFreeEntry;
            container.firstFreeEntry = compHandle;
        } else {
            TECS_LOG_ERROR("Trying to remove component of invalid entity handle! " << handle);
        }
    }

    template <typename T>
    bool entityHasComponent(EntityHandle entity) {
        return entityHasComponent(entity, TypeProvider::template TypeId<T>());
    }

    bool entityHasComponent(EntityHandle entity, u32 componentType) {
        return isEntityHandleValid(entity) 
            && (entities[entity.id].componentsMask & componentType);
    }

    bool isEntityAlive(EntityHandle handle) {
        return entities[handle.id].handle.alive;
    }

    bool isMatchingAllComponents(Entity& entity, u32 mask) {
        return (entity.componentsMask & mask) == mask;
    }

    template<typename T>
    ComponentHandle getEntityComponentHandle(EntityHandle entity) {
        return entities[entity.id].components[TypeProvider::template TypeId<T>()];
    }

    ComponentHandle fetchNewComponentHandle(ComponentContainer &container)
    {
        if (isComponentHandleValid(container.firstFreeEntry.nextFree))
        {
            ComponentHandle newComponent = container.firstFreeEntry.nextFree;
            ensureComponentChunkHandle(container, newComponent);
            container.firstFreeEntry.nextFree = getComponent<ChunkEmptyEntry>(container, newComponent)->nextFree;
            return newComponent;
        }
        else
        {
            return 0;
        }
    }

    ComponentContainer &ensureComponentContainer(u32 typeId, u32 compSize)
    {
        TECS_ASSERT(compSize >= sizeof(ChunkEmptyEntry), "Compsize must be at least size of ChunkEmptyEntry (4 bytes)");
        if (containers[typeId].componentSize == 0)
        {
            containers[typeId].componentSize = compSize;
        }
        return containers[typeId];
    }

    ComponentChunk *ensureComponentChunkHandle(ComponentContainer &container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        if (container.chunks[chunkIdx] == nullptr)
        {
            container.chunks[chunkIdx] = newChunk(container.componentSize, chunkIdx);
        }
        return container.chunks[chunkIdx];
    }

    void *accessComponent(ComponentContainer &container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        u32 insideChunkIdx = handle % componentsPerChunk;
        void *addr = (char *)container.chunks[chunkIdx]->data + container.componentSize * insideChunkIdx;
        return addr;
    }

    template <typename T>
    T *getComponent(ComponentContainer &container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        u32 insideChunkIdx = handle % componentsPerChunk;
        T *addr = (T *)((char *)container.chunks[chunkIdx]->data + container.componentSize * insideChunkIdx);
        return addr;
    }

    // TODO: Change to use arena
    ComponentChunk *newChunk(u32 componentSize, u32 chunkIndex)
    {
        ComponentChunk* c = allocator.alloc<ComponentChunk>(1);
        c->data = allocator.alloc<char>(componentSize * componentsPerChunk); // How many to add per chunk
        for (u32 i = 0; i < componentsPerChunk; ++i)
        {
            ((ChunkEmptyEntry *)((char *)c->data + componentSize * i))->nextFree = chunkIndex*componentsPerChunk + i + 1;
        }
        // Set last as invalid
        // ((ChunkEmptyEntry *)((char *)c->data + componentSize * (componentsPerChunk - 1)))->nextFree = 0;
        return c;
    }

    // TODO: Consider the mask iterator idea
    // template <typename ... Components, typename F>
    // void forEach(F f) {
    //     EntityHandle* list = entityMatchingExact( buildComponentMask<Components...>() );
    //     u32 i = 0;
    //     for (EntityHandle handle = list[0]; handle != 0 && isEntityAlive(handle); ++i) {
    //         Entity& e = entities[handle];
    //         f(e, 
    //             getComponent<Components>(containers[TypeProvider::template TypeId<Components>()], e.components[TypeProvider::template TypeId<Components>()])...);
    //     }
    // }

    template <typename ... Components, typename F>
    void forEach(F f) {
        u32 mask = buildComponentMask<Components...>();
        for (u32 i = 1; i <= liveEntities; ++i) { // TODO: Find smallest component array
            Entity& e = entities[i];
            if (isEntityAlive(e.handle) && isMatchingAllComponents(e, mask)) {
                f(e.handle, *getComponent<Components>(containers[TypeProvider::template TypeId<Components>()], 
                                             e.components[TypeProvider::template TypeId<Components>()])...);
            }
        }
    }

    template<typename T>
    u32 buildComponentMask(T first) {
        return first;
    }

    template<typename T, typename... Args>
    u32 buildComponentMask(T first, Args... args) {
        return first | buildComponentMask(args...);
    }

    template<typename... Args>
    u32 buildComponentMask() {
        return buildComponentMask(TypeProvider::template TypeId<Args>()...);
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