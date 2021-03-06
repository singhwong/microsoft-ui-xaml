// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

// DO NOT EDIT! This file was generated by CustomTasks.DependencyPropertyCodeGen
#pragma once

class IScrollControllerProperties
{
public:
    IScrollControllerProperties();



    winrt::event_token InteractionInfoChanged(winrt::TypedEventHandler<winrt::IScrollController, winrt::IInspectable> const& value);
    void InteractionInfoChanged(winrt::event_token const& token);
    winrt::event_token InteractionRequested(winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerInteractionRequestedEventArgs> const& value);
    void InteractionRequested(winrt::event_token const& token);
    winrt::event_token OffsetChangeRequested(winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerOffsetChangeRequestedEventArgs> const& value);
    void OffsetChangeRequested(winrt::event_token const& token);
    winrt::event_token OffsetChangeWithAdditionalVelocityRequested(winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerOffsetChangeWithAdditionalVelocityRequestedEventArgs> const& value);
    void OffsetChangeWithAdditionalVelocityRequested(winrt::event_token const& token);

    event_source<winrt::TypedEventHandler<winrt::IScrollController, winrt::IInspectable>> m_interactionInfoChangedEventSource;
    event_source<winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerInteractionRequestedEventArgs>> m_interactionRequestedEventSource;
    event_source<winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerOffsetChangeRequestedEventArgs>> m_offsetChangeRequestedEventSource;
    event_source<winrt::TypedEventHandler<winrt::IScrollController, winrt::ScrollControllerOffsetChangeWithAdditionalVelocityRequestedEventArgs>> m_offsetChangeWithAdditionalVelocityRequestedEventSource;

    static void EnsureProperties();
    static void ClearProperties();
};
