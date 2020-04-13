#include <array>
#include <assert.h>
#include <cassert>

typedef unsigned long u32;

namespace tecs
{

template <int MaxComps>
struct TEntity
{
    static constexpr auto MaxComponents = MaxComps;
    int id;
    int componentHandles[MaxComponents];
};
typedef TEntity<64> Entity64;

struct ChunkEmptyEntry
{
    int nextFree;
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

template <typename TypeProvider, typename TheEntity, int MaxEntities_>
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
        if (componentHandleIsValid(nextFreeEntity))
        {
            Entity *e = &entities[nextFreeEntity];
            int id = nextFreeEntity;
            nextFreeEntity = ((ChunkEmptyEntry *)(e))->nextFree;
            ((ChunkEmptyEntry *)(e))->nextFree = 0;
            // TODO: Consider a bitfield variable for checking component handles, so we dont need to clean up the handles list
            *e = {};
            e->id = id;
            return e;
        }
        assert(false); // No more entity space!
        return nullptr;
    }

    void removeEntity(Entity *entity)
    {
        if (componentHandleIsValid(entity->id))
        {
            int prevId = entity->id;
            ((ChunkEmptyEntry *)(entity))->nextFree = nextFreeEntity;
            nextFreeEntity = prevId;
        }
    }

    bool componentHandleIsValid(int handle)
    {
        return handle > 0;
    }

    template <typename T>
    T &addComponent(Entity *entity)
    {
        int compTypeId = TypeProvider::template TypeId<T>();
        if (componentHandleIsValid(entity->componentHandles[compTypeId]))
        {
            return *accessComponentAs<T>(containers[compTypeId], entity->componentHandles[compTypeId]);
        }

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        int handle = addComponentToEntity(container, entity);
        entity->componentHandles[compTypeId] = handle;
        return *accessComponentAs<T>(container, handle);
    }

    template <typename T>
    void removeComponent(Entity *entity)
    {
        int compTypeId = TypeProvider::template TypeId<T>();
        int handle = entity->componentHandles[compTypeId];
        assert(componentHandleIsValid(handle));

        ComponentContainer &container = ensureComponentContainer(compTypeId, sizeof(T));
        accessComponentAs<ChunkEmptyEntry>(container, handle).nextFree = container.firstFreeEntry;
        container.firstFreeEntry = handle;
    }

    int addComponentToEntity(ComponentContainer &container, Entity *entity)
    {
        if (componentHandleIsValid(container.firstFreeEntry.nextFree))
        {
            int newComponent = container.firstFreeEntry.nextFree;
            ensureComponentChunkHandle(container, newComponent);
            container.firstFreeEntry.nextFree = accessComponentAs<ChunkEmptyEntry>(container, newComponent)->nextFree;
            return newComponent;
        }
        else
        {
            // Need to expand chunk
            ComponentChunk *chunk = newChunk(container.componentSize);
            container.chunks[container.nextChunk] = chunk;
            int newComponent = container.nextChunk * componentsPerChunk;
            container.firstFreeEntry.nextFree = newComponent + 1;
            ++container.nextChunk;
            return newComponent;
        }
    }

    ComponentContainer &ensureComponentContainer(int typeId, u32 compSize)
    {
        assert(compSize >= sizeof(ChunkEmptyEntry));
        if (containers[typeId].componentSize == 0)
        {
            containers[typeId].componentSize = compSize;
        }
        return containers[typeId];
    }

    ComponentChunk *ensureComponentChunkHandle(ComponentContainer &container, int handle)
    {
        int chunkIndex = handle / componentsPerChunk;
        if (container.chunks[chunkIndex] == nullptr)
        {
            container.chunks[chunkIndex] = newChunk(container.componentSize);
        }
        return container.chunks[chunkIndex];
    }

    void *accessComponent(ComponentContainer &container, int handle)
    {
        void *addr = (char *)container.chunks[handle / componentsPerChunk]->data + container.componentSize * handle;
        return addr;
    }

    template <typename T>
    T *accessComponentAs(ComponentContainer &container, int handle)
    {
        T *addr = (T *)((char *)container.chunks[handle / componentsPerChunk]->data + container.componentSize * handle);
        return addr;
    }

    ComponentChunk *newChunk(int componentSize)
    {
        ComponentChunk *c = new ComponentChunk();               // TODO: Change to use arena
        c->data = new char[componentSize * componentsPerChunk]; // How many to add per chunk
        for (int i = 0; i < componentsPerChunk; ++i)
        {
            ((ChunkEmptyEntry *)((char *)c->data + componentSize * i))->nextFree = i + 1;
        }
        ((ChunkEmptyEntry *)((char *)c->data + componentSize * (componentsPerChunk - 1)))->nextFree = 0; // Set last as invalid
        return c;
    }

    // TODO:

protected:
    int nextFreeEntity;
    static constexpr int componentsPerChunk = 128;
    std::array<Entity, MaxEntities + 1> entities; // 0 is reserved
    std::array<ComponentContainer, 64> containers;
};

} // namespace tecs