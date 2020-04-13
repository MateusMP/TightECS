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
    u32 size;
};

struct ComponentContainer
{
    u32 componentSize = 0;
    u32 nextChunk = 0;
    ChunkEmptyEntry firstFreeEntry = {1};
    ComponentChunk *chunks[64] = {};
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

    bool componentHandleIsValid(ComponentHandle handle)
    {
        return handle > 0;
    }

    bool entityHandleIsValid(EntityHandle handle)
    {
        return handle > 0;
    }

    template <typename T>
    T &addComponent(Entity *entity)
    {
        u32 compTypeId = TypeProvider::template TypeId<T>();
        if (componentHandleIsValid(entity->components[compTypeId]))
        {
            return *getComponentAs<T>(containers[compTypeId], entity->components[compTypeId]);
        }

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        ComponentHandle handle = addComponentToEntity(container, entity);
        entity->components[compTypeId] = handle;
        return *getComponentAs<T>(container, handle);
    }

    template <typename T>
    void removeComponent(Entity *entity)
    {
        u32 compTypeId = TypeProvider::template TypeId<T>();
        ComponentHandle handle = entity->components[compTypeId];
        assert(componentHandleIsValid(handle));

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        getComponentAs<ChunkEmptyEntry>(container, handle).nextFree = container.firstFreeEntry;
        container.firstFreeEntry = handle;
    }

    ComponentHandle addComponentToEntity(ComponentContainer &container, Entity *entity)
    {
        if (componentHandleIsValid(container.firstFreeEntry.nextFree))
        {
            ComponentHandle newComponent = container.firstFreeEntry.nextFree;
            ensureComponentChunkHandle(container, newComponent);
            container.firstFreeEntry.nextFree = getComponentAs<ChunkEmptyEntry>(container, newComponent)->nextFree;
            return newComponent;
        }
        else
        {
            // Need to expand chunk
            ComponentChunk *chunk = newChunk(container.componentSize);
            container.chunks[container.nextChunk] = chunk;
            ComponentHandle newComponent = container.nextChunk * componentsPerChunk;
            container.firstFreeEntry.nextFree = newComponent + 1;
            ++container.nextChunk;
            return newComponent;
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
            container.chunks[chunkIdx] = newChunk(container.componentSize);
        }
        return container.chunks[chunkIdx];
    }

    void *accessComponent(ComponentContainer &container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        void *addr = (char *)container.chunks[chunkIdx]->data + container.componentSize * handle;
        return addr;
    }

    template <typename T>
    T *getComponentAs(ComponentContainer &container, ComponentHandle handle)
    {
        u32 chunkIdx = handle / componentsPerChunk;
        T *addr = (T *)((char *)container.chunks[chunkIdx]->data + container.componentSize * handle);
        return addr;
    }

    // TODO: Change to use arena
    ComponentChunk *newChunk(u32 componentSize)
    {
        ComponentChunk *c = new ComponentChunk();
        c->data = new char[componentSize * componentsPerChunk]; // How many to add per chunk
        for (u32 i = 0; i < componentsPerChunk; ++i)
        {
            ((ChunkEmptyEntry *)((char *)c->data + componentSize * i))->nextFree = i + 1;
        }
        // Set last as invalid
        ((ChunkEmptyEntry *)((char *)c->data + componentSize * (componentsPerChunk - 1)))->nextFree = 0;
        return c;
    }

    // TODO: forEach<comps>

protected:
    EntityHandle nextFreeEntity;
    static constexpr u32 componentsPerChunk = 128;
    std::array<Entity, MaxEntities + 1> entities; // 0 is reserved
    std::array<ComponentContainer, 64> containers;
};

} // namespace tecs