#ifndef _TECS_H_
#define _TECS_H_

#include <array>
#include <assert.h>
#include <cassert>

#ifndef SKIP_DEFINE_OSTREAM_SERIALIZATION
#include <ostream>
#endif

typedef unsigned long u32;

#define TECS_LOG_ERROR(message) ((void)0);
#define TECS_ASSERT(expression, message) ((void)0);
#define TECS_CHECK(expression, message) if (expression){TECS_LOG_ERROR(message);}


#define CREATE_COMPONENT_TYPES(ComponentTypes) \
    class ComponentTypes { \
    public: \
        template <typename T> \
        static u32 TypeId(); \
    }

#define REGISTER_COMPONENT_TYPE(ComponentTypes, Comp, id) \
    template <>                 \
    u32 ComponentTypes::TypeId<Comp>() { return id; }

namespace tecs
{

    
typedef u32 ComponentHandle;

struct EntityHandleParts {
    u32 alive : 1;
    u32 generation : 3; 
    u32 id : 28;
};

#ifndef SKIP_DEFINE_OSTREAM_SERIALIZATION
std::ostream & operator << (std::ostream &out, const EntityHandleParts &c) {
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

template <typename TypeProvider, unsigned char MaxComponents_, u32 MaxEntities_>
class Ecs
{
protected:
    template <unsigned char MaxComps>
    struct TEntity
    {
        EntityHandle handle;
        ComponentHandle components[MaxComponents_];
    };
public:
    static constexpr auto MaxEntities = MaxEntities_;
    static constexpr auto MaxComponents = MaxComponents_;
    using Entity = TEntity<MaxComponents>;

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

    EntityHandle newEntity()
    {
        if (nextFreeEntity > 0)
        {
            Entity& e = entities[nextFreeEntity];
            u32 id = nextFreeEntity;
            nextFreeEntity = ((ChunkEmptyEntry *)(&e))->nextFree;
            ((ChunkEmptyEntry *)(&e))->nextFree = 0;
            // TODO: Consider a bitfield variable for checking component handles
            // so we dont need to clean up the handles list
            EntityHandle prevHandle = e.handle;
            e = {};
            e.handle = e.handle;
            e.handle.id = id;
            e.handle.alive = 1;
            return e.handle;
        }
        TECS_ASSERT(0, "Can't create more entities!"); // No more entity space!
        return {};
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
            return *getComponent<T>(container, handle);
        }
        throw ("Bad entity handle");
    }

    template <typename T>
    void removeComponent(EntityHandle entityHandle)
    {
        if (isEntityHandleValid(entityHandle)) {

            u32 compTypeId = TypeProvider::template TypeId<T>();
            ComponentHandle compHandle = entities[entityHandle.id].components[compTypeId];
            TECS_CHECK(isComponentHandleValid(compHandle), "Component handle is not valid!");

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
            && isComponentHandleValid(entities[entity.id].components[componentType]);
    }

    bool isEntityAlive(EntityHandle handle) {
        return entities[handle.id].handle.alive;
    }

    bool isMatchingComponents(Entity& entity, u32 mask) {
        return ((entity.components[1]>0)?1:0) & (1 & mask);
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
        for (u32 i = 1; i <= MaxEntities; ++i) { // TODO: Keep track of last alive entity
            Entity& e = entities[i];
            if (isEntityAlive(e.handle) && isMatchingComponents(e, mask)) {
                f(e.handle, *getComponent<Components>(containers[TypeProvider::template TypeId<Components>()], 
                                             e.components[TypeProvider::template TypeId<Components>()])...);
            }
        }
    }

    EntityHandle* entityMatchingExact(u32 mask) {
        return entityIterators[mask];
    }

    template<typename T>
    u32 buildComponentMask(T first) {
        return first;
    }

    template<typename T, typename... Args>
    u32 buildComponentMask(T first, Args... args) {
        return TypeProvider::template TypeId<T>() | buildComponentMask(TypeProvider::template TypeId<Args>()...);
    }

    template<typename... Args>
    u32 buildComponentMask() {
        return buildComponentMask(TypeProvider::template TypeId<Args>()...);
    }



protected:


    u32 nextFreeEntity;
    static constexpr u32 componentsPerChunk = 128;
    std::array<Entity, MaxEntities + 1> entities; // 0 is reserved
    std::array<ComponentContainer, MaxComponents> containers;

    std::array<EntityHandle*, MaxComponents> entityIterators;
};

} // namespace tecs

#endif