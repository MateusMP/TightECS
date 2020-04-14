#include <array>
#include <assert.h>
#include <cassert>

typedef unsigned long u32;

namespace tecs
{

typedef u32 ComponentHandle;
typedef u32 EntityHandle;

template <u32 MaxComps>
struct TEntity
{
    static constexpr auto MaxComponents = MaxComps;
    EntityHandle id;
    ComponentHandle components[MaxComponents];
};
typedef TEntity<64> Entity64;


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

template <typename TypeProvider, typename TheEntity, u32 MaxEntities_>
class Ecs
{
public:
    using Entity = TheEntity;
    static constexpr auto MaxEntities = MaxEntities_;
    static constexpr auto MaxComponents = TheEntity::MaxComponents;

    Ecs()
    {
        init();
    }

    void init()
    {
        nextFreeEntity = 0;
        entities = {};
        containers = {};
        for (Entity &e : entities)
        {
            ((ChunkEmptyEntry &)(e)).nextFree = ++nextFreeEntity;
        }
        ((ChunkEmptyEntry *)(entities.end() - 1))->nextFree = 0;
        nextFreeEntity = 1;
    }

    Entity *newEntity()
    {
        if (entityHandleIsValid(nextFreeEntity))
        {
            Entity *e = &entities[nextFreeEntity];
            u32 id = nextFreeEntity;
            nextFreeEntity = ((ChunkEmptyEntry *)(e))->nextFree;
            ((ChunkEmptyEntry *)(e))->nextFree = 0;
            // TODO: Consider a bitfield variable for checking component handles
            // so we dont need to clean up the handles list
            *e = {};
            e->id = id;
            return e;
        }
        assert(false); // No more entity space!
        return nullptr;
    }

    void removeEntity(Entity *entity)
    {
        if (entityHandleIsValid(entity->id))
        {
            EntityHandle prevId = entity->id;
            ((ChunkEmptyEntry *)(entity))->nextFree = nextFreeEntity;
            nextFreeEntity = prevId;
        }
    }

    bool isComponentHandleValid(ComponentHandle handle)
    {
        return handle > 0 && handle < componentsPerChunk * MaxComponentChunks;
    }

    bool entityHandleIsValid(EntityHandle handle)
    {
        return handle > 0;
    }

    template <typename T>
    T &addComponent(Entity *entity)
    {
        u32 compTypeId = TypeProvider::template TypeId<T>();
        if (isComponentHandleValid(entity->components[compTypeId]))
        {
            return *getComponent<T>(containers[compTypeId], entity->components[compTypeId]);
        }

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        ComponentHandle handle = addComponentToEntity(container, entity);
        entity->components[compTypeId] = handle;
        return *getComponent<T>(container, handle);
    }

    template <typename T>
    void removeComponent(Entity *entity)
    {
        u32 compTypeId = TypeProvider::template TypeId<T>();
        ComponentHandle handle = entity->components[compTypeId];
        assert(isComponentHandleValid(handle));

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        getComponent<ChunkEmptyEntry>(container, handle).nextFree = container.firstFreeEntry;
        container.firstFreeEntry = handle;
    }

    ComponentHandle addComponentToEntity(ComponentContainer &container, Entity *entity)
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
        assert(compSize >= sizeof(ChunkEmptyEntry));
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
        ComponentChunk *c = new ComponentChunk();
        c->data = new char[componentSize * componentsPerChunk]; // How many to add per chunk
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
        for (EntityHandle i = 1; i <= MaxEntities; ++i) {
            Entity& e = entities[i];
            if (isEntityAlive(i) && isMatchingComponents(e, mask)) {
                f(e, getComponent<Components>(containers[TypeProvider::template TypeId<Components>()], 
                                             e.components[TypeProvider::template TypeId<Components>()])...);
            }
        }
    }

    bool isEntityAlive(EntityHandle handle) {
        // TODO
        return true;
    }

    bool isMatchingComponents(Entity& entity, u32 mask) {
        return ((entity.components[1]>0)?1:0) & (1 & mask);
    }

    EntityHandle* entityMatchingExact(u32 mask) {
        return entityIterators[mask];
    }

    template<typename ...T>
    u32 buildComponentMask() {
        u32 m = {0 | TypeProvider::template TypeId<T>() ... };
        return m;
    }


protected:


    EntityHandle nextFreeEntity;
    static constexpr u32 componentsPerChunk = 128;
    std::array<Entity, MaxEntities + 1> entities; // 0 is reserved
    std::array<ComponentContainer, Entity::MaxComponents> containers;

    std::array<EntityHandle*, MaxComponents> entityIterators;
};

} // namespace tecs