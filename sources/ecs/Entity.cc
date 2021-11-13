#include <v2d/ecs/Entity.hh>

namespace v2d {

std::size_t EntityManager::s_component_family_counter = 0;

void Entity::destroy() {
    m_manager->destroy_entity(m_id);
}

Entity EntityManager::create_entity() {
    // TODO: Entity id recycling.
    m_count++;
    return {m_next_id++, this};
}

void EntityManager::destroy_entity(EntityId) {
    V2D_ENSURE_NOT_REACHED();
}

} // namespace v2d