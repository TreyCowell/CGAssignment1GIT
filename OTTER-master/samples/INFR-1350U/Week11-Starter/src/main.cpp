#include <Logging.h>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <json.hpp>
#include <fstream>

#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/type_ptr.hpp>

#include "Graphics/IndexBuffer.h"
#include "Graphics/VertexBuffer.h"
#include "Graphics/VertexArrayObject.h"
#include "Graphics/Shader.h"
#include "Gameplay/Camera.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Behaviours/CameraControlBehaviour.h"
#include "Behaviours/FollowPathBehaviour.h"
#include "Behaviours/SimpleMoveBehaviour.h"
#include "Gameplay/Application.h"
#include "Gameplay/GameObjectTag.h"
#include "Gameplay/IBehaviour.h"
#include "Gameplay/Transform.h"
#include "Graphics/Texture2D.h"
#include "Graphics/Texture2DData.h"
#include "Utilities/InputHelpers.h"
#include "Utilities/MeshBuilder.h"
#include "Utilities/MeshFactory.h"
#include "Utilities/NotObjLoader.h"
#include "Utilities/ObjLoader.h"
#include "Utilities/VertexTypes.h"
#include "Gameplay/Scene.h"
#include "Gameplay/ShaderMaterial.h"
#include "Gameplay/RendererComponent.h"
#include "Gameplay/Timing.h"
#include "Graphics/TextureCubeMap.h"
#include "Graphics/TextureCubeMapData.h"

#define LOG_GL_NOTIFICATIONS

/*
	Handles debug messages from OpenGL
	https://www.khronos.org/opengl/wiki/Debug_Output#Message_Components
	@param source    Which part of OpenGL dispatched the message
	@param type      The type of message (ex: error, performance issues, deprecated behavior)
	@param id        The ID of the error or message (to distinguish between different types of errors, like nullref or index out of range)
	@param severity  The severity of the message (from High to Notification)
	@param length    The length of the message
	@param message   The human readable message from OpenGL
	@param userParam The pointer we set with glDebugMessageCallback (should be the game pointer)
*/
void GlDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	std::string sourceTxt;
	switch (source) {
	case GL_DEBUG_SOURCE_API: sourceTxt = "DEBUG"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceTxt = "WINDOW"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceTxt = "SHADER"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY: sourceTxt = "THIRD PARTY"; break;
	case GL_DEBUG_SOURCE_APPLICATION: sourceTxt = "APP"; break;
	case GL_DEBUG_SOURCE_OTHER: default: sourceTxt = "OTHER"; break;
	}
	switch (severity) {
	case GL_DEBUG_SEVERITY_LOW:          LOG_INFO("[{}] {}", sourceTxt, message); break;
	case GL_DEBUG_SEVERITY_MEDIUM:       LOG_WARN("[{}] {}", sourceTxt, message); break;
	case GL_DEBUG_SEVERITY_HIGH:         LOG_ERROR("[{}] {}", sourceTxt, message); break;
		#ifdef LOG_GL_NOTIFICATIONS
	case GL_DEBUG_SEVERITY_NOTIFICATION: LOG_INFO("[{}] {}", sourceTxt, message); break;
		#endif
	default: break;
	}
}

GLFWwindow* window;

void GlfwWindowResizedCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	Application::Instance().ActiveScene->Registry().view<Camera>().each([=](Camera & cam) {
		cam.ResizeWindow(width, height);
	});
}

bool InitGLFW() {
	if (glfwInit() == GLFW_FALSE) {
		LOG_ERROR("Failed to initialize GLFW");
		return false;
	}

#ifdef _DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif
	
	//Create a new GLFW window
	window = glfwCreateWindow(800, 800, "INFR1350U", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	// Set our window resized callback
	glfwSetWindowSizeCallback(window, GlfwWindowResizedCallback);

	// Store the window in the application singleton
	Application::Instance().Window = window;

	return true;
}

bool InitGLAD() {
	if (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) == 0) {
		LOG_ERROR("Failed to initialize Glad");
		return false;
	}
	return true;
}

void InitImGui() {
	// Creates a new ImGUI context
	ImGui::CreateContext();
	// Gets our ImGUI input/output 
	ImGuiIO& io = ImGui::GetIO();
	// Enable keyboard navigation
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// Allow docking to our window
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// Allow multiple viewports (so we can drag ImGui off our window)
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	// Allow our viewports to use transparent backbuffers
	io.ConfigFlags |= ImGuiConfigFlags_TransparentBackbuffers;

	// Set up the ImGui implementation for OpenGL
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 410");

	// Dark mode FTW
	ImGui::StyleColorsDark();

	// Get our imgui style
	ImGuiStyle& style = ImGui::GetStyle();
	//style.Alpha = 1.0f;
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 0.8f;
	}
}

void ShutdownImGui()
{
	// Cleanup the ImGui implementation
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	// Destroy our ImGui context
	ImGui::DestroyContext();
}

std::vector<std::function<void()>> imGuiCallbacks;
void RenderImGui() {
	// Implementation new frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	// ImGui context new frame
	ImGui::NewFrame();

	if (ImGui::Begin("Debug")) {
		// Render our GUI stuff
		for (auto& func : imGuiCallbacks) {
			func();
		}
		ImGui::End();
	}
	
	// Make sure ImGui knows how big our window is
	ImGuiIO& io = ImGui::GetIO();
	int width{ 0 }, height{ 0 };
	glfwGetWindowSize(window, &width, &height);
	io.DisplaySize = ImVec2((float)width, (float)height);

	// Render all of our ImGui elements
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// If we have multiple viewports enabled (can drag into a new window)
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		// Update the windows that ImGui is using
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		// Restore our gl context
		glfwMakeContextCurrent(window);
	}
}

void RenderVAO(
	const Shader::sptr& shader,
	const VertexArrayObject::sptr& vao,
	const glm::mat4& viewProjection,
	const Transform& transform)
{
	shader->SetUniformMatrix("u_ModelViewProjection", viewProjection * transform.WorldTransform());
	shader->SetUniformMatrix("u_Model", transform.WorldTransform()); 
	shader->SetUniformMatrix("u_NormalMatrix", transform.WorldNormalMatrix());
	vao->Render();
}

void SetupShaderForFrame(const Shader::sptr& shader, const glm::mat4& view, const glm::mat4& projection) {
	shader->Bind();
	// These are the uniforms that update only once per frame
	shader->SetUniformMatrix("u_View", view);
	shader->SetUniformMatrix("u_ViewProjection", projection * view);
	shader->SetUniformMatrix("u_SkyboxMatrix", projection * glm::mat4(glm::mat3(view)));
	glm::vec3 camPos = glm::inverse(view) * glm::vec4(0,0,0,1);
	shader->SetUniform("u_CamPos", camPos);
}

int main() {
	Logger::Init(); // We'll borrow the logger from the toolkit, but we need to initialize it

	//Initialize GLFW
	if (!InitGLFW())
		return 1;

	//Initialize GLAD
	if (!InitGLAD())
		return 1;

	int frameIx = 0;
	float fpsBuffer[128];
	float minFps, maxFps, avgFps;
	int selectedVao = 0; // select cube by default
	std::vector<GameObject> controllables;

	// Let OpenGL know that we want debug output, and route it to our handler function
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(GlDebugMessage, nullptr);

	// Enable texturing
	glEnable(GL_TEXTURE_2D);

	// Push another scope so most memory should be freed *before* we exit the app
	{
		#pragma region Shader and ImGui

		// Load our shaders
		Shader::sptr shader = Shader::Create();
		shader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		shader->LoadShaderPartFromFile("shaders/frag_blinn_phong_textured.glsl", GL_FRAGMENT_SHADER);
		shader->Link();

		glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 2.0f);
		glm::vec3 lightCol = glm::vec3(0.9f, 0.85f, 0.5f);
		float     lightAmbientPow = 1.5f;
		float     lightSpecularPow = 1.0f;
		float texUV = 0.0f;
		glm::vec3 ambientCol = glm::vec3(1.0f);
		float     ambientPow = 0.1f;
		float     lightLinearFalloff = 0.09f;
		float     lightQuadraticFalloff = 0.032f;
		int noLight = 0;
		int ambLight = 0;
		int specLight = 0;
		int specAmLight = 0;
		int otherToon = 0;

		// These are our application / scene level uniforms that don't necessarily update
		// every frame
		shader->SetUniform("u_LightPos", lightPos);
		shader->SetUniform("u_LightCol", lightCol);
		shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
		shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
		shader->SetUniform("u_AmbientCol", ambientCol);
		shader->SetUniform("u_AmbientStrength", ambientPow);
		shader->SetUniform("u_LightAttenuationConstant", 1.0f);
		shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
		shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		shader->SetUniform("u_NoLighting", noLight);
		shader->SetUniform("u_Ambient", ambLight);
		shader->SetUniform("u_Specular", specLight);
		shader->SetUniform("u_AmbientAndSpecular", specAmLight);
		shader->SetUniform("u_AmbientSpecularToon", otherToon);

		// We'll add some ImGui controls to control our shader
		imGuiCallbacks.push_back([&]() {
			if (ImGui::CollapsingHeader("Scene Level Lighting Settings"))
			{
				if (ImGui::ColorPicker3("Ambient Color", glm::value_ptr(ambientCol))) {
					shader->SetUniform("u_AmbientCol", ambientCol);
				}
				if (ImGui::SliderFloat("Fixed Ambient Power", &ambientPow, 0.01f, 1.0f)) {
					shader->SetUniform("u_AmbientStrength", ambientPow);
				}
			}
			if (ImGui::CollapsingHeader("Light Level Lighting Settings"))
			{
				if (ImGui::DragFloat3("Light Pos", glm::value_ptr(lightPos), 0.01f, -10.0f, 10.0f)) {
					shader->SetUniform("u_LightPos", lightPos);
				}
				if (ImGui::ColorPicker3("Light Col", glm::value_ptr(lightCol))) {
					shader->SetUniform("u_LightCol", lightCol);
				}
				if (ImGui::SliderFloat("Light Ambient Power", &lightAmbientPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
				}
				if (ImGui::SliderFloat("Light Specular Power", &lightSpecularPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				if (ImGui::DragFloat("Light Linear Falloff", &lightLinearFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
				}
				if (ImGui::DragFloat("Light Quadratic Falloff", &lightQuadraticFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
				}
			}
			if (ImGui::CollapsingHeader("Light Requirements for Assignment 1"))
			{
				//Turn lighting off and on button
				if (ImGui::Button("No Lighting"))
				{
					shader->SetUniform("u_NoLighting", noLight = 1);
					shader->SetUniform("u_Ambient", ambLight = 0);
					shader->SetUniform("u_Specular", specLight = 0);
					shader->SetUniform("u_AmbientAndSpecular", specAmLight = 0);
					shader->SetUniform("u_AmbientSpecularToon", otherToon = 0);
				}
				//Turn on the ambient lighting only
				if (ImGui::Button("Ambient Lighting"))
				{
					shader->SetUniform("u_NoLighting", noLight = 0);
					shader->SetUniform("u_Ambient", ambLight = 1);
					shader->SetUniform("u_Specular", specLight = 0);
					shader->SetUniform("u_AmbientAndSpecular", specAmLight = 0);
					shader->SetUniform("u_AmbientSpecularToon", otherToon = 0);
				}
				//Turn on the specular lighting only
				if (ImGui::Button("Specular Lighting"))
				{
					shader->SetUniform("u_NoLighting", noLight = 0);
					shader->SetUniform("u_Ambient", ambLight = 0);
					shader->SetUniform("u_Specular", specLight = 1);
					shader->SetUniform("u_AmbientAndSpecular", specAmLight = 0);
					shader->SetUniform("u_AmbientSpecularToon", otherToon = 0);
				}
				//Turn on the ambient and specular lighting together
				if (ImGui::Button("Ambient and Specular Lighting"))
				{
					shader->SetUniform("u_NoLighting", noLight = 0);
					shader->SetUniform("u_Ambient", ambLight = 0);
					shader->SetUniform("u_Specular", specLight = 0);
					shader->SetUniform("u_AmbientAndSpecular", specAmLight = 1);
					shader->SetUniform("u_AmbientSpecularToon", otherToon = 0);
				}
				//Turn on the ambient and specular lighting together
				if (ImGui::Button("Other Effect"))
				{
					shader->SetUniform("u_NoLighting", noLight = 0);
					shader->SetUniform("u_Ambient", ambLight = 0);
					shader->SetUniform("u_Specular", specLight = 0);
					shader->SetUniform("u_AmbientAndSpecular", specAmLight = 0);
					shader->SetUniform("u_AmbientSpecularToon", otherToon = 1);
				}
			}
			auto name = controllables[selectedVao].get<GameObjectTag>().Name;
			ImGui::Text(name.c_str());
			auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
			ImGui::Checkbox("Relative Rotation", &behaviour->Relative);

			ImGui::Text("Q/E -> Yaw\nLeft/Right -> Roll\nUp/Down -> Pitch\nY -> Toggle Mode");
		
			minFps = FLT_MAX;
			maxFps = 0;
			avgFps = 0;
			for (int ix = 0; ix < 128; ix++) {
				if (fpsBuffer[ix] < minFps) { minFps = fpsBuffer[ix]; }
				if (fpsBuffer[ix] > maxFps) { maxFps = fpsBuffer[ix]; }
				avgFps += fpsBuffer[ix];
			}
			ImGui::PlotLines("FPS", fpsBuffer, 128);
			ImGui::Text("MIN: %f MAX: %f AVG: %f", minFps, maxFps, avgFps / 128.0f);
			});

		#pragma endregion 

		// GL states
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL); // New 

		#pragma region TEXTURE LOADING

		// Load some textures from files
		Texture2D::sptr diffuse = Texture2D::LoadFromFile("images/Stone_001_Diffuse.png");
		Texture2D::sptr bottle = Texture2D::LoadFromFile("images/BottleTex.png");
		Texture2D::sptr table = Texture2D::LoadFromFile("images/Table.png");
		Texture2D::sptr blackChess = Texture2D::LoadFromFile("images/blackChess.jpg");
		Texture2D::sptr whiteChess = Texture2D::LoadFromFile("images/whiteChess.jpg");
		Texture2D::sptr DunceSkin = Texture2D::LoadFromFile("images/SkinPNG.png");
		Texture2D::sptr SliceOfCake = Texture2D::LoadFromFile("images/Slice of Cake.png");
		Texture2D::sptr diffuse2 = Texture2D::LoadFromFile("images/box.bmp");
		Texture2D::sptr specular = Texture2D::LoadFromFile("images/Stone_001_Specular.png");
		Texture2D::sptr reflectivity = Texture2D::LoadFromFile("images/box-reflections.bmp");

		// Load the cube map
		//TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/sample.jpg");
		TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/ocean.jpg"); 

		// Creating an empty texture
		Texture2DDescription desc = Texture2DDescription();  
		desc.Width = 1;
		desc.Height = 1;
		desc.Format = InternalFormat::RGB8;
		Texture2D::sptr texture2 = Texture2D::Create(desc);
		// Clear it with a white colour
		texture2->Clear();

		#pragma endregion

		///////////////////////////////////// Scene Generation //////////////////////////////////////////////////
		#pragma region Scene Generation
		
		// We need to tell our scene system what extra component types we want to support
		GameScene::RegisterComponentType<RendererComponent>();
		GameScene::RegisterComponentType<BehaviourBinding>();
		GameScene::RegisterComponentType<Camera>();

		// Create a scene, and set it to be the active scene in the application
		GameScene::sptr scene = GameScene::Create("test");
		Application::Instance().ActiveScene = scene;

		// We can create a group ahead of time to make iterating on the group faster
		entt::basic_group<entt::entity, entt::exclude_t<>, entt::get_t<Transform>, RendererComponent> renderGroup =
			scene->Registry().group<RendererComponent>(entt::get_t<Transform>());

		// Create a material and set some properties for it
		ShaderMaterial::sptr material0 = ShaderMaterial::Create();  
		material0->Shader = shader;
		material0->Set("s_Diffuse", diffuse);
		material0->Set("s_Diffuse2", diffuse2);
		material0->Set("s_Specular", specular);
		material0->Set("u_Shininess", 8.0f);
		material0->Set("u_TextureMix", 0.5f); 

		ShaderMaterial::sptr material2 = ShaderMaterial::Create();//bottle material
		material2->Shader = shader;
		material2->Set("s_Diffuse", bottle);
		material2->Set("s_Diffuse2", diffuse2);
		material2->Set("s_Specular", specular);
		material2->Set("u_Shininess", 8.0f);
		material2->Set("u_TextureMix", 0.2f);

		ShaderMaterial::sptr material3 = ShaderMaterial::Create();//table material
		material3->Shader = shader;
		material3->Set("s_Diffuse", table);
		material3->Set("s_Diffuse2", diffuse2);
		material3->Set("s_Specular", specular);
		material3->Set("u_Shininess", 8.0f);
		material3->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr material4 = ShaderMaterial::Create();//black material
		material4->Shader = shader;
		material4->Set("s_Diffuse", blackChess);
		material4->Set("s_Diffuse2", diffuse2);
		material4->Set("s_Specular", specular);
		material4->Set("u_Shininess", 8.0f);
		material4->Set("u_TextureMix", 0.2f);

		ShaderMaterial::sptr material5 = ShaderMaterial::Create();//white material
		material5->Shader = shader;
		material5->Set("s_Diffuse", whiteChess);
		material5->Set("s_Diffuse2", diffuse2);
		material5->Set("s_Specular", specular);
		material5->Set("u_Shininess", 8.0f);
		material5->Set("u_TextureMix", 0.2f);

		ShaderMaterial::sptr material6 = ShaderMaterial::Create();//dunce material
		material6->Shader = shader;
		material6->Set("s_Diffuse", DunceSkin);
		material6->Set("s_Diffuse2", diffuse2);
		material6->Set("s_Specular", specular);
		material6->Set("u_Shininess", 8.0f);
		material6->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr material7 = ShaderMaterial::Create();//cake material
		material7->Shader = shader;
		material7->Set("s_Diffuse", SliceOfCake);
		material7->Set("s_Diffuse2", diffuse2);
		material7->Set("s_Specular", specular);
		material7->Set("u_Shininess", 8.0f);
		material7->Set("u_TextureMix", 0.0f);

		// Load a second material for our reflective material!
		Shader::sptr reflectiveShader = Shader::Create();
		reflectiveShader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflectiveShader->LoadShaderPartFromFile("shaders/frag_reflection.frag.glsl", GL_FRAGMENT_SHADER);
		reflectiveShader->Link();

		Shader::sptr reflective = Shader::Create();
		reflective->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflective->LoadShaderPartFromFile("shaders/frag_blinn_phong_reflection.glsl", GL_FRAGMENT_SHADER);
		reflective->Link();
		
		// 
		ShaderMaterial::sptr material1 = ShaderMaterial::Create(); 
		material1->Shader = reflective;
		material1->Set("s_Diffuse", diffuse);
		material1->Set("s_Diffuse2", diffuse2);
		material1->Set("s_Specular", specular);
		material1->Set("s_Reflectivity", reflectivity); 
		material1->Set("s_Environment", environmentMap); 
		material1->Set("u_LightPos", lightPos);
		material1->Set("u_LightCol", lightCol);
		material1->Set("u_AmbientLightStrength", lightAmbientPow); 
		material1->Set("u_SpecularLightStrength", lightSpecularPow); 
		material1->Set("u_AmbientCol", ambientCol);
		material1->Set("u_AmbientStrength", ambientPow);
		material1->Set("u_LightAttenuationConstant", 1.0f);
		material1->Set("u_LightAttenuationLinear", lightLinearFalloff);
		material1->Set("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		material1->Set("u_Shininess", 8.0f);
		material1->Set("u_TextureMix", 0.5f);
		material1->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(90.0f), glm::radians(90.0f), glm::vec3(0, 0, 1))));
		
		ShaderMaterial::sptr reflectiveMat = ShaderMaterial::Create();
		reflectiveMat->Shader = reflectiveShader;
		reflectiveMat->Set("s_Environment", environmentMap);
		reflectiveMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(90.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));

		GameObject sceneObj = scene->CreateEntity("Table"); 
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Table.obj");
			sceneObj.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material3);
			sceneObj.get<Transform>().SetLocalPosition(0.0f, -4.0f, -4.0f);
			sceneObj.get<Transform>().SetLocalScale(2.0f, 2.0f, 2.0f);
			sceneObj.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
		}

		GameObject obj2 = scene->CreateEntity("waterBottle");//left one
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/waterBottle.obj");
			obj2.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material2);
			obj2.get<Transform>().SetLocalPosition(3.0f, -4.0f, 0.5f);
			obj2.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj2);
		}

		GameObject obj3 = scene->CreateEntity("chessPawn");//fallen one
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj3.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material5);
			obj3.get<Transform>().SetLocalPosition(2.0f, 0.0f, 0.6f);
			obj3.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj3.get<Transform>().SetLocalRotation(355.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}


		GameObject obj5 = scene->CreateEntity("chessPawn2");//first upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj5.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material5);
			obj5.get<Transform>().SetLocalPosition(2.0f, -0.6f, 0.5f);
			obj5.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj5.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj5);
		}

		GameObject obj8 = scene->CreateEntity("chessPawn3");//second fallen
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj8.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material4);
			obj8.get<Transform>().SetLocalPosition(-2.0f, 0.3f, 0.7f);
			obj8.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj8.get<Transform>().SetLocalRotation(355.0f, 0.0f, 90.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj8);
		}


		GameObject obj9 = scene->CreateEntity("chessPawn4");//third upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj9.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material4);
			obj9.get<Transform>().SetLocalPosition(-2.0f, -0.6f, 0.5f);
			obj9.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj9.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj9);
		}

		GameObject obj10 = scene->CreateEntity("chessPawn5");//fourth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj10.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material5);
			obj10.get<Transform>().SetLocalPosition(2.0f, -1.6f, 0.5f);
			obj10.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj10.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj10);
		}


		GameObject obj11 = scene->CreateEntity("chessPawn6");//fifth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj11.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material4);
			obj11.get<Transform>().SetLocalPosition(-2.0f, -1.6f, 0.5f);
			obj11.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj11.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj11);
		}

		GameObject obj12 = scene->CreateEntity("chessPawn7");//sixth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj12.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material5);
			obj12.get<Transform>().SetLocalPosition(1.3f, -1.6f, 0.5f);
			obj12.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj12.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj12);
		}


		GameObject obj13 = scene->CreateEntity("chessPawn8");//eigth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj13.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material4);
			obj13.get<Transform>().SetLocalPosition(-1.3f, -1.6f, 0.5f);
			obj13.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj13.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj13);
		}

		GameObject obj14 = scene->CreateEntity("chessPawn9");//ninth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj14.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material5);
			obj14.get<Transform>().SetLocalPosition(1.3f, -0.6f, 0.5f);
			obj14.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj14.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj14);
		}


		GameObject obj15 = scene->CreateEntity("chessPawn10");//tenth upright
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ChessPawn.obj");
			obj15.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material4);
			obj15.get<Transform>().SetLocalPosition(-1.3f, -0.6f, 0.5f);
			obj15.get<Transform>().SetLocalScale(0.15f, 0.15f, 0.15f);
			obj15.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj15);
		}

		GameObject obj7 = scene->CreateEntity("waterBottle2");//right one
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/waterBottle.obj");
			obj7.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material2);
			obj7.get<Transform>().SetLocalPosition(-4.0f, -4.0f, 0.5f);
			obj7.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj7);
		}

		GameObject obj4 = scene->CreateEntity("Rolling Water");
		{
			// Build a mesh
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/waterBottle.obj");
			
			obj4.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material2);
			obj4.get<Transform>().SetLocalPosition(-2.0f, 0.0f, 1.0f);

			// Bind returns a smart pointer to the behaviour that was added
			auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj4);
			// Set up a path for the object to follow
			pathing->Points.push_back({ 3.0f,  3.0f, 1.0f });
			pathing->Points.push_back({ -3.0f,  3.0f, 1.0f });
			pathing->Speed = 1.0f;
		}

		GameObject obj6 = scene->CreateEntity("Jumping Dunce");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Dunce.obj");
			obj6.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material6);
			obj6.get<Transform>().SetLocalPosition(-7.0f, -2.0f, 3.0f);
			obj6.get<Transform>().SetLocalScale(1.5f, 1.5f, 1.5f);
			obj6.get<Transform>().SetLocalRotation(90.0f, 0.0f, 90.0f);
			
			auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj6);
			// Set up a path for the object to follow
			pathing->Points.push_back({ -7.0f, -2.0f, -3.0f });
			pathing->Points.push_back({ -7.0f, -2.0f, 2.0f });
			pathing->Speed = 3.0f;
		}

		GameObject obj16 = scene->CreateEntity("cake");//left one
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/SliceofCake.obj");
			obj16.emplace<RendererComponent>().SetMesh(vao).SetMaterial(material7);
			obj16.get<Transform>().SetLocalPosition(0.0f, -7.0f, 1.2f);
			obj16.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj16);
		}
		
		// Create an object to be our camera
		GameObject cameraObject = scene->CreateEntity("Camera");
		{
			cameraObject.get<Transform>().SetLocalPosition(0, 6, 6).LookAt(glm::vec3(0, 0, 0));

			// We'll make our camera a component of the camera object
			Camera& camera = cameraObject.emplace<Camera>();// Camera::Create();
			camera.SetPosition(glm::vec3(0, 6, 6));
			camera.SetUp(glm::vec3(0, 0, 1));
			camera.LookAt(glm::vec3(0));
			camera.SetFovDegrees(90.0f); // Set an initial FOV
			camera.SetOrthoHeight(3.0f);
			BehaviourBinding::Bind<CameraControlBehaviour>(cameraObject);
		}

		#pragma endregion 
		//////////////////////////////////////////////////////////////////////////////////////////

		/////////////////////////////////// SKYBOX ///////////////////////////////////////////////
		{
			// Load our shaders
			Shader::sptr skybox = std::make_shared<Shader>();
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.vert.glsl", GL_VERTEX_SHADER);
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.frag.glsl", GL_FRAGMENT_SHADER);
			skybox->Link();

			ShaderMaterial::sptr skyboxMat = ShaderMaterial::Create();
			skyboxMat->Shader = skybox;  
			skyboxMat->Set("s_Environment", environmentMap);
			skyboxMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
			skyboxMat->RenderLayer = 100;

			MeshBuilder<VertexPosNormTexCol> mesh;
			MeshFactory::AddIcoSphere(mesh, glm::vec3(0.0f), 1.0f);
			MeshFactory::InvertFaces(mesh);
			VertexArrayObject::sptr meshVao = mesh.Bake();
			
			GameObject skyboxObj = scene->CreateEntity("skybox");  
			skyboxObj.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			skyboxObj.get_or_emplace<RendererComponent>().SetMesh(meshVao).SetMaterial(skyboxMat);
		}
		////////////////////////////////////////////////////////////////////////////////////////


		// We'll use a vector to store all our key press events for now (this should probably be a behaviour eventually)
		std::vector<KeyPressWatcher> keyToggles;
		{
			// This is an example of a key press handling helper. Look at InputHelpers.h an .cpp to see
			// how this is implemented. Note that the ampersand here is capturing the variables within
			// the scope. If you wanted to do some method on the class, your best bet would be to give it a method and
			// use std::bind
			keyToggles.emplace_back(GLFW_KEY_T, [&]() { cameraObject.get<Camera>().ToggleOrtho(); });

			controllables.push_back(obj2);
			controllables.push_back(obj3);

			keyToggles.emplace_back(GLFW_KEY_KP_ADD, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao++;
				if (selectedVao >= controllables.size())
					selectedVao = 0;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});
			keyToggles.emplace_back(GLFW_KEY_KP_SUBTRACT, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao--;
				if (selectedVao < 0)
					selectedVao = controllables.size() - 1;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});

			keyToggles.emplace_back(GLFW_KEY_Y, [&]() {
				auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
				behaviour->Relative = !behaviour->Relative;
				});
		}
		
		InitImGui();

		// Initialize our timing instance and grab a reference for our use
		Timing& time = Timing::Instance();
		time.LastFrame = glfwGetTime();

		///// Game loop /////
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			// Update the timing
			time.CurrentFrame = glfwGetTime();
			time.DeltaTime = static_cast<float>(time.CurrentFrame - time.LastFrame);

			time.DeltaTime = time.DeltaTime > 1.0f ? 1.0f : time.DeltaTime;

			// Update our FPS tracker data
			fpsBuffer[frameIx] = 1.0f / time.DeltaTime;
			frameIx++;
			if (frameIx >= 128)
				frameIx = 0;

			// We'll make sure our UI isn't focused before we start handling input for our game
			if (!ImGui::IsAnyWindowFocused()) {
				// We need to poll our key watchers so they can do their logic with the GLFW state
				// Note that since we want to make sure we don't copy our key handlers, we need a const
				// reference!
				for (const KeyPressWatcher& watcher : keyToggles) {
					watcher.Poll(window);
				}
			}

			// Iterate over all the behaviour binding components
			scene->Registry().view<BehaviourBinding>().each([&](entt::entity entity, BehaviourBinding& binding) {
				// Iterate over all the behaviour scripts attached to the entity, and update them in sequence (if enabled)
				for (const auto& behaviour : binding.Behaviours) {
					if (behaviour->Enabled) {
						behaviour->Update(entt::handle(scene->Registry(), entity));
					}
				}
			});

			// Clear the screen
			glClearColor(0.08f, 0.17f, 0.31f, 1.0f);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Update all world matrices for this frame
			scene->Registry().view<Transform>().each([](entt::entity entity, Transform& t) {
				t.UpdateWorldMatrix();
			});
			
			// Grab out camera info from the camera object
			Transform& camTransform = cameraObject.get<Transform>();
			glm::mat4 view = glm::inverse(camTransform.LocalTransform());
			glm::mat4 projection = cameraObject.get<Camera>().GetProjection();
			glm::mat4 viewProjection = projection * view;
						
			// Sort the renderers by shader and material, we will go for a minimizing context switches approach here,
			// but you could for instance sort front to back to optimize for fill rate if you have intensive fragment shaders
			renderGroup.sort<RendererComponent>([](const RendererComponent& l, const RendererComponent& r) {
				// Sort by render layer first, higher numbers get drawn last
				if (l.Material->RenderLayer < r.Material->RenderLayer) return true;
				if (l.Material->RenderLayer > r.Material->RenderLayer) return false;

				// Sort by shader pointer next (so materials using the same shader run sequentially where possible)
				if (l.Material->Shader < r.Material->Shader) return true;
				if (l.Material->Shader > r.Material->Shader) return false;

				// Sort by material pointer last (so we can minimize switching between materials)
				if (l.Material < r.Material) return true;
				if (l.Material > r.Material) return false;
				
				return false;
			});

			// Start by assuming no shader or material is applied
			Shader::sptr current = nullptr;
			ShaderMaterial::sptr currentMat = nullptr;

			// Iterate over the render group components and draw them
			renderGroup.each( [&](entt::entity e, RendererComponent& renderer, Transform& transform) {
				// If the shader has changed, set up it's uniforms
				if (current != renderer.Material->Shader) {
					current = renderer.Material->Shader;
					current->Bind();
					SetupShaderForFrame(current, view, projection);
				}
				// If the material has changed, apply it
				if (currentMat != renderer.Material) {
					currentMat = renderer.Material;
					currentMat->Apply();
				}
				// Render the mesh
				RenderVAO(renderer.Material->Shader, renderer.Mesh, viewProjection, transform);
			});

			// Draw our ImGui content
			RenderImGui();

			scene->Poll();
			glfwSwapBuffers(window);
			time.LastFrame = time.CurrentFrame;
		}

		// Nullify scene so that we can release references
		Application::Instance().ActiveScene = nullptr;
		ShutdownImGui();
	}	

	// Clean up the toolkit logger so we don't leak memory
	Logger::Uninitialize();
	return 0;
}