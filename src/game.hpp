#pragma once

struct GLFWwindow;

class Game
{
public:
    virtual ~Game() = default;

    virtual void update(GLFWwindow* window) = 0;
    virtual void draw() = 0;
};

extern Game* createGame();
