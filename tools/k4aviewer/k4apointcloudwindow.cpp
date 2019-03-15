// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// Associated header
//
#include "k4apointcloudwindow.h"

// System headers
//
#include <sstream>
#include <utility>

// Library headers
//
#include "k4aimgui_all.h"

// Project headers
//
#include "k4aimguiextensions.h"
#include "k4aviewererrormanager.h"
#include "k4awindowsizehelpers.h"

using namespace k4aviewer;

namespace
{
constexpr int DefaultPointSize = 2;
}

void K4APointCloudWindow::Show(K4AWindowPlacementInfo placementInfo)
{
    if (m_failed)
    {
        ImGui::Text("Frame source failed!");
        return;
    }

    if (m_captureSource->IsFailed())
    {
        K4AViewerErrorManager::Instance().SetErrorStatus(m_title + ": frame source failed!");
        m_failed = true;
        return;
    }

    if (m_captureSource->HasData())
    {
        const PointCloudVisualizationResult visualizationResult =
            m_pointCloudVisualizer.UpdateTexture(&m_texture, m_captureSource->GetLastCapture());
        if (!CheckVisualizationResult(visualizationResult))
        {
            // Fatal error.
            //
            return;
        }

        m_consecutiveMissingImages = 0;
    }

    ImVec2 availableSize = placementInfo.Size;
    availableSize.y -= 3 * ImGui::GetTextLineHeightWithSpacing(); // Instructions text
    availableSize.y -= GetDefaultButtonHeight();                  // Mode radio buttons
    availableSize.y -= GetDefaultButtonHeight();                  // Reset button

    const ImVec2 sourceImageSize = ImVec2(static_cast<float>(m_texture->GetDimensions().Width),
                                          static_cast<float>(m_texture->GetDimensions().Height));
    const ImVec2 textureSize = GetImageSize(sourceImageSize, availableSize);

    ImGui::Image(static_cast<ImTextureID>(*m_texture), textureSize);

    ImGui::BeginGroup();
    ImGui::Text("Movement: W/S/A/D/[Ctrl]/[Space]");
    ImGui::Text("Look: [Right Mouse] + Drag");
    ImGui::Text("Zoom: Mouse wheel");
    ImGui::EndGroup();

    if (m_missingColorImages != 0 || m_missingDepthImages != 0)
    {
        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            ImGuiExtensions::TextColorChanger warningColorChanger(ImGuiExtensions::TextColor::Warning);
            ImGui::Text("%s", "Warning: some captures were dropped due to missing images!");
            ImGui::Text("Missing depth images: %d", m_missingDepthImages);
            ImGui::Text("Missing color images: %d", m_missingColorImages);
        }
        ImGui::EndGroup();

        if (!m_haveShownMissingImagesWarning)
        {
            std::stringstream warningBuilder;
            warningBuilder
                << "Warning: Some captures didn't have both color and depth data and had to be dropped." << std::endl
                << "         To avoid this, you can set the \"Synchronized images only\" option under Internal Sync.";
            K4AViewerErrorManager::Instance().SetErrorStatus(warningBuilder.str());
            m_haveShownMissingImagesWarning = true;
        }
    }

    int *ipColorizationStrategy = reinterpret_cast<int *>(&m_colorizationStrategy);

    bool colorizationStrategyUpdated = false;
    colorizationStrategyUpdated |=
        ImGuiExtensions::K4ARadioButton("Simple",
                                        ipColorizationStrategy,
                                        static_cast<int>(K4APointCloudVisualizer::ColorizationStrategy::Simple));
    ImGui::SameLine();
    colorizationStrategyUpdated |=
        ImGuiExtensions::K4ARadioButton("Shaded",
                                        ipColorizationStrategy,
                                        static_cast<int>(K4APointCloudVisualizer::ColorizationStrategy::Shaded));
    ImGui::SameLine();
    colorizationStrategyUpdated |=
        ImGuiExtensions::K4ARadioButton("Color (BGRA only)",
                                        ipColorizationStrategy,
                                        static_cast<int>(K4APointCloudVisualizer::ColorizationStrategy::Color),
                                        m_enableColorPointCloud);

    if (colorizationStrategyUpdated)
    {
        PointCloudVisualizationResult result = m_pointCloudVisualizer.SetColorizationStrategy(m_colorizationStrategy);
        if (!CheckVisualizationResult(result))
        {
            return;
        }
    }

    if (ImGui::SliderInt("", &m_pointSize, 1, 10, "Point Size: %d px"))
    {
        m_pointCloudVisualizer.SetPointSize(m_pointSize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset view"))
    {
        m_pointCloudVisualizer.ResetPosition();
        m_pointSize = DefaultPointSize;
        m_pointCloudVisualizer.SetPointSize(m_pointSize);
    }

    ProcessInput();
}

const char *K4APointCloudWindow::GetTitle() const
{
    return m_title.c_str();
}

K4APointCloudWindow::K4APointCloudWindow(std::string &&windowTitle,
                                         bool enableColorPointCloud,
                                         std::shared_ptr<K4ANonBufferingCaptureSource> &&captureSource,
                                         const k4a::calibration &calibrationData) :
    m_title(std::move(windowTitle)),
    m_pointCloudVisualizer(enableColorPointCloud, calibrationData),
    m_captureSource(std::move(captureSource)),
    m_pointSize(DefaultPointSize),
    m_enableColorPointCloud(enableColorPointCloud)
{
    GLenum initResult = m_pointCloudVisualizer.InitializeTexture(&m_texture);
    if (initResult != GL_NO_ERROR)
    {
        CheckVisualizationResult(PointCloudVisualizationResult::OpenGlError);
    }

    m_pointCloudVisualizer.SetPointSize(m_pointSize);
    CheckVisualizationResult(m_pointCloudVisualizer.SetColorizationStrategy(m_colorizationStrategy));
    m_lastTime = glfwGetTime();
}

void K4APointCloudWindow::ProcessInput()
{
    const double currentTime = glfwGetTime();
    const auto timeDelta = static_cast<float>(currentTime - m_lastTime);
    m_lastTime = currentTime;

    if (ImGui::IsWindowFocused())
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.KeysDown[GLFW_KEY_W])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Forward, timeDelta);
        }
        if (io.KeysDown[GLFW_KEY_A])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Left, timeDelta);
        }
        if (io.KeysDown[GLFW_KEY_D])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Right, timeDelta);
        }
        if (io.KeysDown[GLFW_KEY_S])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Backward, timeDelta);
        }
        if (io.KeysDown[GLFW_KEY_SPACE])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Down, timeDelta);
        }
        if (io.KeysDown[GLFW_KEY_LEFT_CONTROL])
        {
            m_pointCloudVisualizer.ProcessPositionalMovement(ViewMovement::Up, timeDelta);
        }

        if (io.MouseDown[GLFW_MOUSE_BUTTON_2]) // right-click
        {
            m_pointCloudVisualizer.ProcessMouseMovement(io.MouseDelta.x, io.MouseDelta.y);
        }

        m_pointCloudVisualizer.ProcessMouseScroll(io.MouseWheel);
    }
}

bool K4APointCloudWindow::CheckVisualizationResult(PointCloudVisualizationResult visualizationResult)
{
    switch (visualizationResult)
    {
    case PointCloudVisualizationResult::Success:
        return true;
    case PointCloudVisualizationResult::MissingDepthImage:
        ++m_consecutiveMissingImages;
        ++m_missingDepthImages;
        break;
    case PointCloudVisualizationResult::MissingColorImage:
        ++m_consecutiveMissingImages;
        ++m_missingColorImages;
        break;
    case PointCloudVisualizationResult::OpenGlError:
        SetFailed("OpenGL error!");
        return false;
    case PointCloudVisualizationResult::DepthToXyzTransformationFailed:
        SetFailed("Depth -> XYZ transformation failed!");
        return false;
    case PointCloudVisualizationResult::DepthToColorTransformationFailed:
        SetFailed("Depth -> Color transformation failed!");
        return false;
    }

    if (m_consecutiveMissingImages >= MaxConsecutiveMissingImages)
    {
        if (visualizationResult == PointCloudVisualizationResult::MissingDepthImage)
        {
            SetFailed("Stopped receiving depth data!");
            return false;
        }
        if (visualizationResult == PointCloudVisualizationResult::MissingColorImage)
        {
            SetFailed("Stopped receiving color data!");
            return false;
        }
    }

    return true;
}

void K4APointCloudWindow::SetFailed(const char *msg)
{
    std::stringstream errorBuilder;
    errorBuilder << m_title << ": " << msg;
    K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
    m_failed = true;
}