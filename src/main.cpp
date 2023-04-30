#include <iostream>
#include <memory>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "game.hpp"

struct GLFWWrapper
{
    GLFWWrapper()
    {
        if (!glfwInit())
        {
            throw std::runtime_error("glfwInit failed");
        }
    }

    ~GLFWWrapper()
    {
        glfwTerminate();
    }
};

static GLFWwindow* createWindow(int width, int height, const char* title)
{
    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create window");
    }
    glfwSetWindowSizeLimits(window, width, height, width, height);
    return window;
}

int main(int argc, char** argv)
{
    GLFWWrapper glfwWrapper;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow* window = createWindow(1920, 1080, "Window");
    glfwMakeContextCurrent(window);

    if (!gladLoadGL())
    {
        throw std::runtime_error("gladLoadGL failed");
    }

    std::unique_ptr<Game> game(createGame());

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        game->update(window);
        game->draw();

        glfwSwapBuffers(window);
    }

    return 0;
}
