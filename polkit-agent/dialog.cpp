// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialog.h"

#include <cmath>

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/design_metrics.h"
#include "ui/dialog_window.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/painter.h"
#include "ui/text_field.h"
#include "ui/theme.h"
#include "ui/window.h"

using namespace cutefish::ui;
using cutefish::i18n::tr;

static constexpr double kWidth = 390;
// A drag this far from the press moves the window.
static constexpr double kDragDistance = 10;

// The rounded card the window shows, and the layout on it; any empty part of
// it drags the window, as there is no title bar.
class Dialog::Content final : public ColumnLayout
{
public:
    bool acceptsPointer() const override { return true; }

protected:
    void pointerPress(const PointerEvent &event) override
    {
        m_pressed = event.button == MouseButton::Left;
        m_pressPosition = event.position;
    }

    void pointerMove(const PointerEvent &event) override
    {
        if (!m_pressed || !window())
            return;
        if (std::abs(event.position.x - m_pressPosition.x) > kDragDistance
            || std::abs(event.position.y - m_pressPosition.y) > kDragDistance) {
            m_pressed = false;
            window()->startMove();
        }
    }

    void pointerRelease(const PointerEvent &) override { m_pressed = false; }

private:
    bool m_pressed = false;
    PointF m_pressPosition;
};

Dialog::Dialog(const std::string &message, const std::string &identity)
    : m_message(message)
    , m_identity(identity)
{
}

Dialog::~Dialog() = default;

void Dialog::show()
{
    m_window = std::make_unique<Window>();
    m_window->setTitle(tr("Authentication Required"));
    m_window->setDialogAppearance();
    // Text lays out unhinted at every scale, so lines wrap where they always did.
    m_window->setHintedTextLayout(false);
    m_window->setAlwaysOnTop(true);
    m_window->setBackgroundColor(ColorId::PanelBackground);
    m_window->onCloseRequested = [this] {
        if (onCancel)
            onCancel();
    };

    auto content = std::make_unique<Content>();
    content->setPadding(Margins::all(design::dialogInset));
    content->setSpacing(12);

    auto *icon = content->addChild<ThemeIcon>("dialog-password", 32);
    icon->setLayoutHints({.preferredWidth = 32, .preferredHeight = 32});

    // A Column, not a layout: its labels are as wide as the window less the margins.
    auto *texts = content->addChild<Column>();
    texts->setSpacing(4);
    texts->setLayoutHints({.fillWidth = true});
    auto *title = texts->addChild<Label>(tr("Authentication Required"));
    title->setLayoutHints({.fillWidth = true});
    title->setFontSize(14);
    title->setWeight(600);
    auto *message = texts->addChild<Label>(m_message);
    message->setLayoutHints({.fillWidth = true});
    message->setFontSize(14);
    message->setWrap(true);
    message->setFixedLineHeight(20);
    message->setColor(ColorId::DisabledText);

    auto *identity = content->addChild<Label>(m_identity);
    identity->setElide(Label::Elide::Middle);
    identity->setLayoutHints({.fillWidth = true});

    m_passwordInput = content->addChild<TextField>();
    m_passwordInput->setLayoutHints({.fillWidth = true});
    m_passwordInput->setName("authenticationResponse");
    m_passwordInput->setPlaceholder(m_prompt.empty() ? tr("Password") : m_prompt);
    m_passwordInput->setClipboardEnabled(false);
    m_passwordInput->setPasswordCharacter("●");
    m_passwordInput->setEchoMode(m_echo ? TextField::EchoMode::Normal : TextField::EchoMode::Password);
    m_passwordInput->onAccepted = [this] {
        submit();
    };
    m_passwordInput->onTextChanged = [this] {
        m_doneButton->setEnabled(m_passwordInput->isEnabled() && !m_passwordInput->text().empty());
    };

    m_noticeLabel = content->addChild<Label>();
    m_noticeLabel->setLayoutHints({.fillWidth = true});
    m_noticeLabel->setFontSize(14);
    m_noticeLabel->setWrap(true);

    auto *buttons = content->addChild<RowLayout>();
    buttons->setSpacing(8);
    buttons->setLayoutHints({.horizontalAlignment = Alignment::End});
    auto *cancel = buttons->addChild<DialogButton>(tr("Cancel"));
    cancel->onClicked = [this] {
        if (onCancel)
            onCancel();
    };
    m_doneButton = buttons->addChild<DialogButton>(tr("Authenticate"), true);
    m_doneButton->setEnabled(false);
    m_doneButton->onClicked = [this] {
        submit();
    };

    m_window->addShortcut(NoModifier, key::Escape, [this] {
        if (onCancel)
            onCancel();
    });

    updateNotice();
    m_window->setContentView(std::move(content));
    m_window->resize({kWidth, 0});
    m_window->setFitsContentHeight(true);
    m_passwordInput->setFocus();
    m_window->show();
    m_window->activate();
}

void Dialog::hide()
{
    if (m_window)
        m_window->hide();
}

void Dialog::submit()
{
    if (!m_passwordInput || !m_passwordInput->isEnabled() || m_passwordInput->text().empty())
        return;

    m_doneButton->setEnabled(false);
    m_passwordInput->setEnabled(false);
    m_password = m_passwordInput->text();

    if (onAccepted)
        onAccepted();
}

void Dialog::ready()
{
    m_doneButton->setEnabled(!m_passwordInput->text().empty());
    m_passwordInput->setEnabled(true);
    m_passwordInput->setFocus();
}

void Dialog::authenticationFailure()
{
    if (!m_passwordInput)
        return;
    // Enter would otherwise send the rejected password again.
    m_passwordInput->setText({});
    ready();
}

void Dialog::setRequest(const std::string &prompt, bool echo)
{
    m_prompt = prompt;
    m_echo = echo;

    if (!m_passwordInput)
        return;
    m_passwordInput->setPlaceholder(m_prompt.empty() ? tr("Password") : m_prompt);
    m_passwordInput->setEchoMode(m_echo ? TextField::EchoMode::Normal : TextField::EchoMode::Password);

    // Each further PAM question starts empty; a field still being typed in is
    // left alone.
    if (!m_passwordInput->isEnabled())
        m_passwordInput->setText({});
    ready();
}

void Dialog::setNotice(const std::string &text, bool error)
{
    if (m_notice == text && m_noticeIsError == error)
        return;

    m_notice = text;
    m_noticeIsError = error;
    updateNotice();
}

void Dialog::updateNotice()
{
    if (!m_noticeLabel)
        return;
    m_noticeLabel->setText(m_notice);
    m_noticeLabel->setVisible(!m_notice.empty());
    m_noticeLabel->setColor(m_noticeIsError ? ColorRecipe(accent::red) : ColorRecipe(ColorId::DisabledText));
}
