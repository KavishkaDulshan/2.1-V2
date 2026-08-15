#ifndef CHAT_UI_H
#define CHAT_UI_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <vector>

struct ChatMessage {
    String text;
    bool isUser;
};

class ChatUI {
public:
    ChatUI();
    void init(lgfx::LGFX_Device* display);
    void addMessage(String text, bool isUser);
    void appendLastMessage(String text);
    
    // Call every frame in loop()
    void update(bool isTouched, int touchY); 
    void draw();
    void forceRedraw() { _needsRedraw = true; }
    const std::vector<ChatMessage>& getMessages() const { return _messages; }

private:
    lgfx::LGFX_Device* _display;
    LGFX_Sprite* _sprite;
    std::vector<ChatMessage> _messages;
    
    float _scrollOffsetY;
    float _velocity;
    int _lastTouchY;
    bool _isDragging;
    bool _needsRedraw;
    unsigned long _lastTouchTime;

    void drawWrappedText(String text, int x, int y, int w, bool isUser, int& outHeight);
    int calculateTotalHeight();
    int calculateMessageHeight(const ChatMessage& msg);
    
    static const int MAX_MESSAGES = 25;
};

#endif
