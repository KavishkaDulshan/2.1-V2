#include "ChatUI.h"

ChatUI::ChatUI() {
    _display = nullptr;
    _sprite = nullptr;
    _scrollOffsetY = 0.0f;
    _velocity = 0.0f;
    _lastTouchY = 0;
    _isDragging = false;
    _needsRedraw = true;
    _lastTouchTime = 0;
}

void ChatUI::init(lgfx::LGFX_Device* display) {
    _display = display;
    _sprite = new LGFX_Sprite(_display);
    // 240x192 16-bit sprite using PSRAM if available
    _sprite->setPsram(true);
    _sprite->createSprite(240, 192);
    _sprite->setTextSize(1);
    // Use a small, clean font
    _sprite->setFont(&fonts::Font2);
}

void ChatUI::addMessage(String text, bool isUser) {
    if (_messages.size() >= MAX_MESSAGES) {
        _messages.erase(_messages.begin());
    }
    _messages.push_back({text, isUser});
    
    // Auto-scroll to bottom when a new message arrives
    int totalH = calculateTotalHeight();
    if (totalH > 192) {
        _scrollOffsetY = totalH - 192;
    } else {
        _scrollOffsetY = 0;
    }
    _velocity = 0;
    _needsRedraw = true;
}

void ChatUI::update(bool isTouched, int touchY) {
    unsigned long now = millis();
    int totalH = calculateTotalHeight();
    float maxScroll = (totalH > 192) ? (totalH - 192) : 0;

    if (isTouched) {
        if (!_isDragging) {
            _isDragging = true;
            _velocity = 0;
        } else {
            // Delta Y: dragging UP scrolls down into messages (natural scrolling)
            float delta = _lastTouchY - touchY;
            if (delta != 0) {
                _scrollOffsetY += delta;
                _needsRedraw = true;
                
                float dt = (now - _lastTouchTime) / 1000.0f;
                if (dt > 0.001f && dt < 0.1f) {
                    // Reduce instantaneous sensitivity and clamp maximum fling velocity
                    float instantVel = (delta / dt) * 0.35f;
                    if (instantVel > 450.0f) instantVel = 450.0f;
                    if (instantVel < -450.0f) instantVel = -450.0f;
                    // Smooth velocity using a low-pass filter
                    _velocity = (_velocity * 0.7f) + (instantVel * 0.3f);
                }
            } else if (now - _lastTouchTime > 25) {
                // Finger stopped moving before release; kill momentum
                _velocity *= 0.3f;
            }
        }
        _lastTouchY = touchY;
        _lastTouchTime = now;
    } else {
        if (_isDragging) {
            _isDragging = false;
        }
        
        // Apply smooth kinetic friction with stronger damping
        if (abs(_velocity) > 5.0f) {
            _scrollOffsetY += _velocity * 0.016f;
            _velocity *= 0.82f; // Increased friction to prevent over-shooting and sliding too far
            _needsRedraw = true;
        } else {
            _velocity = 0;
        }
    }

    // Clamp scroll
    float oldScroll = _scrollOffsetY;
    if (_scrollOffsetY < 0) {
        _scrollOffsetY = 0;
        _velocity = 0;
    } else if (_scrollOffsetY > maxScroll) {
        _scrollOffsetY = maxScroll;
        _velocity = 0;
    }
    if (_scrollOffsetY != oldScroll) {
        _needsRedraw = true;
    }
}

int ChatUI::calculateMessageHeight(const ChatMessage& msg) {
    _sprite->setFont(&fonts::Font2);
    int maxWidth = 180; // Bubbles don't take full width
    // Very basic line height estimation
    int charWidth = 8; // approx width of Font2 char
    int charsPerLine = maxWidth / charWidth;
    int lines = (msg.text.length() / charsPerLine) + 1;
    return (lines * 16) + 10; // 16px line height + 10px padding
}

int ChatUI::calculateTotalHeight() {
    int total = 0;
    for (const auto& msg : _messages) {
        total += calculateMessageHeight(msg) + 5; // +5 margin
    }
    return total;
}

void ChatUI::drawWrappedText(String text, int x, int y, int w, bool isUser, int& outHeight) {
    _sprite->setFont(&fonts::Font2);
    
    // Draw bubble background
    int h = calculateMessageHeight({text, isUser});
    outHeight = h;
    
    if (isUser) {
        // Right aligned, green bubble
        _sprite->fillRoundRect(x, y, w, h, 8, _sprite->color565(40, 140, 60));
        _sprite->setTextColor(TFT_WHITE);
    } else {
        // Left aligned, dark gray bubble
        _sprite->fillRoundRect(x, y, w, h, 8, _sprite->color565(60, 60, 60));
        _sprite->setTextColor(TFT_LIGHTGRAY);
    }
    
    // Cursor to draw text
    _sprite->setCursor(x + 5, y + 5);
    
    // Simple wrap logic
    int currentX = x + 5;
    int currentY = y + 5;
    
    int lastSpace = -1;
    int lineStart = 0;
    
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == ' ') lastSpace = i;
        
        String currentLine = text.substring(lineStart, i + 1);
        if (_sprite->textWidth(currentLine) > w - 10) {
            // Need to wrap
            if (lastSpace > lineStart) {
                // Wrap at last space
                _sprite->drawString(text.substring(lineStart, lastSpace), currentX, currentY);
                i = lastSpace;
                lineStart = lastSpace + 1;
            } else {
                // Wrap mid-word
                _sprite->drawString(text.substring(lineStart, i), currentX, currentY);
                lineStart = i;
            }
            currentY += 16;
            currentX = x + 5;
        } else if (i == text.length() - 1) {
            // Last piece
            _sprite->drawString(text.substring(lineStart, i + 1), currentX, currentY);
        }
    }
}

void ChatUI::draw() {
    if (!_needsRedraw) return;

    _sprite->fillSprite(TFT_BLACK);
    
    int currentY = 5 - (int)_scrollOffsetY;
    
    for (const auto& msg : _messages) {
        int bubbleW = 180;
        int msgH = 0;
        
        // Skip drawing if completely off-screen
        int estH = calculateMessageHeight(msg);
        if (currentY + estH + 5 < 0) {
            currentY += estH + 5;
            continue;
        }
        if (currentY > 192) {
            break; // rest are below screen
        }

        if (msg.isUser) {
            // User message on the right
            int x = 240 - bubbleW - 5;
            drawWrappedText(msg.text, x, currentY, bubbleW, true, msgH);
        } else {
            // Robot message on the left
            int x = 5;
            drawWrappedText(msg.text, x, currentY, bubbleW, false, msgH);
        }
        
        currentY += msgH + 5;
    }
    
    _sprite->pushSprite(0, 128); // Draw to bottom of 240x320 screen
    _needsRedraw = false;
}
