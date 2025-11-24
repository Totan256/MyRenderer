#include "Renderer.hpp"
#include <glm/glm.hpp>
struct SceneData {
    glm::vec4 resolution; // width, height, 0, 0
    glm::vec4 params;     // time, frame, 0, 0
    glm::vec4 cameraPos;
};
// int m_width;
// int m_height;
// std::unique_ptr<GraphicsDevice> m_device;
// std::unique_ptr<DescriptorManager> m_descManager;
// std::unique_ptr<CommandList> m_cmdList;
// std::unique_ptr<ComputePipeline> m_raytracePipeline;
Renderer::Renderer(int width, int height){
    m_width = width;
    m_height = height;
    m_device->initialize();
}

Renderer::~Renderer(){

}

void Renderer::initialize(){

}

void Renderer::render(){

}

void Renderer::saveImage(const std::string& filename){

}