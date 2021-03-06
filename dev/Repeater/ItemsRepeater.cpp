﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include <pch.h>
#include <common.h>
#include "ItemsRepeater.common.h"
#include "ItemsRepeater.h"
#include "RepeaterFactory.h"
#include "RepeaterLayoutContext.h"
#include "ChildrenInTabFocusOrderIterable.h"
#include "SharedHelpers.h"
#include "RepeaterAutomationPeer.h"
#include "ViewportManagerWithPlatformFeatures.h"
#include "ViewportManagerDownlevel.h"
#include "RuntimeProfiler.h"

#ifndef BUILD_WINDOWS
#include "ItemTemplateWrapper.h"
#endif

winrt::Point ItemsRepeater::ClearedElementsArrangePosition = winrt::Point(-10000.0f, -10000.0f);
winrt::Rect ItemsRepeater::InvalidRect = { -1.f, -1.f, -1.f, -1.f };

ItemsRepeater::ItemsRepeater()
{
    __RP_Marker_ClassById(RuntimeProfiler::ProfId_ItemsRepeater);

    if (SharedHelpers::IsRS5OrHigher())
    {
        m_viewportManager = std::make_shared<ViewportManagerWithPlatformFeatures>(this);
    }
    else
    {
        m_viewportManager = std::make_shared<ViewportManagerDownLevel>(this);
    }

    winrt::AutomationProperties::SetAccessibilityView(*this, winrt::AccessibilityView::Raw);
    if (SharedHelpers::IsRS3OrHigher())
    {
        TabFocusNavigation(winrt::KeyboardNavigationMode::Once);
        XYFocusKeyboardNavigation(winrt::XYFocusKeyboardNavigationMode::Enabled);
    }

    Loaded({ this, &ItemsRepeater::OnLoaded });
    Unloaded({ this, &ItemsRepeater::OnUnloaded });

    EnsureProperties();
    // Initialize the cached layout to the default value
    auto layout = GetValue(s_layoutProperty).as<winrt::VirtualizingLayout>();
    OnLayoutChanged(nullptr, layout);
}

ItemsRepeater::~ItemsRepeater()
{
    if (m_layout)
    {
        if (m_measureInvalidated)
        {
            m_layout.MeasureInvalidated(m_measureInvalidated);
        }

        if (m_arrangeInvalidated)
        {
            m_layout.ArrangeInvalidated(m_arrangeInvalidated);
        }
    }
}

#pragma region IUIElementOverrides

winrt::AutomationPeer ItemsRepeater::OnCreateAutomationPeer()
{
    return winrt::make<RepeaterAutomationPeer>(*this);
}

#pragma endregion

#pragma region IUIElementOverrides7

winrt::IIterable<winrt::DependencyObject> ItemsRepeater::GetChildrenInTabFocusOrder()
{
    return CreateChildrenInTabFocusOrderIterable();
}

#pragma endregion

#pragma region IUIElementOverrides8

void ItemsRepeater::OnBringIntoViewRequested(winrt::BringIntoViewRequestedEventArgs const& e)
{
    m_viewportManager->OnBringIntoViewRequested(e);
}

#pragma endregion

#pragma region IFrameworkElementOverrides

winrt::Size ItemsRepeater::MeasureOverride(winrt::Size const& availableSize)
{
    if (m_isLayoutInProgress)
    {
        throw winrt::hresult_error(E_FAIL, L"Reentrancy detected during layout.");
    }

    if (IsProcessingCollectionChange())
    {
        throw winrt::hresult_error(E_FAIL, L"Cannot run layout in the middle of a collection change.");
    }

    m_viewportManager->OnOwnerMeasuring();

    m_isLayoutInProgress = true;
    auto layoutInProgress = gsl::finally([this]()
    {
        m_isLayoutInProgress = false;
    });

    m_viewManager.PrunePinnedElements();
    winrt::Rect extent{};
    winrt::Size desiredSize{};

    if (m_layout)
    {
        desiredSize = m_layout.Measure(GetLayoutContext(), availableSize);
        extent = winrt::Rect{ m_layoutOrigin.X, m_layoutOrigin.Y, desiredSize.Width, desiredSize.Height };

        // Clear auto recycle candidate elements that have not been kept alive by layout - i.e layout did not
        // call GetElementAt(index).
        auto children = Children();
        for (unsigned i = 0u; i < children.Size(); ++i)
        {
            auto element = children.GetAt(i);
            auto virtInfo = GetVirtualizationInfo(element);

            if (virtInfo->Owner() == ElementOwner::Layout &&
                virtInfo->AutoRecycleCandidate() &&
                !virtInfo->KeepAlive())
            {
                REPEATER_TRACE_INFO(L"AutoClear - %d \n", virtInfo->Index());
                ClearElementImpl(element);
            }
        }
    }

    m_viewportManager->SetLayoutExtent(extent);
    m_lastAvailableSize = availableSize;
    return desiredSize;
}

winrt::Size ItemsRepeater::ArrangeOverride(winrt::Size const& finalSize)
{
    if (m_isLayoutInProgress)
    {
        throw winrt::hresult_error(E_FAIL, L"Reentrancy detected during layout.");
    }

    if (IsProcessingCollectionChange())
    {
        throw winrt::hresult_error(E_FAIL, L"Cannot run layout in the middle of a collection change.");
    }

    m_isLayoutInProgress = true;
    auto layoutInProgress = gsl::finally([this]()
    {
        m_isLayoutInProgress = false;
    });

    winrt::Size arrangeSize{};

    if (m_layout)
    {
        arrangeSize = m_layout.Arrange(GetLayoutContext(), finalSize);
    }

    // The view manager might clear elements during this call.
    // That's why we call it before arranging cleared elements
    // off screen.
    m_viewManager.OnOwnerArranged();

    auto children = Children();
    for (unsigned i = 0u; i < children.Size(); ++i)
    {
        auto element = children.GetAt(i);
        auto virtInfo = GetVirtualizationInfo(element);
        virtInfo->KeepAlive(false);

        if (virtInfo->Owner() == ElementOwner::ElementFactory ||
            virtInfo->Owner() == ElementOwner::PinnedPool)
        {
            // Toss it away. And arrange it with size 0 so that XYFocus won't use it.
            element.Arrange(winrt::Rect{
                ClearedElementsArrangePosition.X - static_cast<float>(element.DesiredSize().Width),
                ClearedElementsArrangePosition.Y - static_cast<float>(element.DesiredSize().Height),
                0.0f,
                0.0f });
        }
        else
        {
            const auto newBounds = CachedVisualTreeHelpers::GetLayoutSlot(element.as<winrt::FrameworkElement>());

            if (virtInfo->ArrangeBounds() != ItemsRepeater::InvalidRect &&
                newBounds != virtInfo->ArrangeBounds())
            {
                m_animationManager.OnElementBoundsChanged(element, virtInfo->ArrangeBounds(), newBounds);
            }

            virtInfo->ArrangeBounds(newBounds);
        }
    }

    m_viewportManager->OnOwnerArranged();
    m_animationManager.OnOwnerArranged();

    return arrangeSize;
}

#pragma endregion

#pragma region IRepeater interface.

winrt::IInspectable ItemsRepeater::ItemsSource()
{
    return GetValue(s_itemsSourceProperty);
}

void ItemsRepeater::ItemsSource(winrt::IInspectable const& value)
{
    SetValue(s_itemsSourceProperty, value);
}

winrt::ItemsSourceView ItemsRepeater::ItemsSourceView()
{
    return m_dataSource.get();
}

winrt::IElementFactory ItemsRepeater::ItemTemplate()
{
    return m_itemTemplate;
}

void ItemsRepeater::ItemTemplate(winrt::IElementFactory const& value)
{
    SetValue(s_itemTemplateProperty, value);
}

winrt::VirtualizingLayout ItemsRepeater::Layout()
{
    return m_layout;
}

void ItemsRepeater::Layout(winrt::VirtualizingLayout const& value)
{
    SetValue(s_layoutProperty, value);
}

winrt::ElementAnimator ItemsRepeater::Animator()
{
    return m_animator;
}

void ItemsRepeater::Animator(winrt::ElementAnimator const& value)
{
    SetValue(s_animatorProperty, value);
}

double ItemsRepeater::HorizontalCacheLength()
{
    return m_viewportManager->HorizontalCacheLength();
}

void ItemsRepeater::HorizontalCacheLength(double value)
{
    SetValue(s_horizontalCacheLengthProperty, box_value(value));
}

double ItemsRepeater::VerticalCacheLength()
{
    return m_viewportManager->VerticalCacheLength();
}

void ItemsRepeater::VerticalCacheLength(double value)
{
    SetValue(s_verticalCacheLengthProperty, box_value(value));
}

winrt::Brush ItemsRepeater::Background()
{
    return ValueHelper<winrt::Brush>::CastOrUnbox((this)->GetValue(BackgroundProperty()));
}

void ItemsRepeater::Background(winrt::Brush const& value)
{
    SetValue(BackgroundProperty(), value);
}

int32_t ItemsRepeater::GetElementIndex(winrt::UIElement const& element)
{
    return GetElementIndexImpl(element);
}

winrt::UIElement ItemsRepeater::TryGetElement(int index)
{
    return GetElementFromIndexImpl(index);
}

void ItemsRepeater::PinElement(winrt::UIElement const& element)
{
    m_viewManager.UpdatePin(element, true /* addPin */);
}

void ItemsRepeater::UnpinElement(winrt::UIElement const& element)
{
    m_viewManager.UpdatePin(element, false /* addPin */);
}

winrt::UIElement ItemsRepeater::GetOrCreateElement(int index)
{
    return GetOrCreateElementImpl(index);
}

winrt::event_token ItemsRepeater::ElementPrepared(winrt::TypedEventHandler<winrt::ItemsRepeater, winrt::ItemsRepeaterElementPreparedEventArgs> const& value)
{
    return m_elementPreparedEventSource.add(value);
}

void ItemsRepeater::ElementPrepared(winrt::event_token const& token)
{
    m_elementPreparedEventSource.remove(token);
}

winrt::event_token ItemsRepeater::ElementClearing(winrt::TypedEventHandler<winrt::ItemsRepeater, winrt::ItemsRepeaterElementClearingEventArgs> const& value)
{
    return m_elementClearingEventSource.add(value);
}

void ItemsRepeater::ElementClearing(winrt::event_token const& token)
{
    m_elementClearingEventSource.remove(token);
}

winrt::event_token ItemsRepeater::ElementIndexChanged(winrt::TypedEventHandler<winrt::ItemsRepeater, winrt::ItemsRepeaterElementIndexChangedEventArgs> const& value)
{
    return m_elementIndexChangedEventSource.add(value);
}

void ItemsRepeater::ElementIndexChanged(winrt::event_token const& token)
{
    m_elementIndexChangedEventSource.remove(token);
}

#pragma endregion

winrt::UIElement ItemsRepeater::GetElementImpl(int index, bool forceCreate, bool suppressAutoRecycle)
{
    auto element = m_viewManager.GetElement(index, forceCreate, suppressAutoRecycle);
    return element;
}

void ItemsRepeater::ClearElementImpl(const winrt::UIElement& element)
{
    // Clearing an element due to a collection change
    // is more strict in that pinned elements will be forcibly
    // unpinned and sent back to the view generator.
    const bool isClearedDueToCollectionChange =
        IsProcessingCollectionChange() &&
        (m_processingDataSourceChange.get().Action() == winrt::NotifyCollectionChangedAction::Remove ||
            m_processingDataSourceChange.get().Action() == winrt::NotifyCollectionChangedAction::Replace ||
            m_processingDataSourceChange.get().Action() == winrt::NotifyCollectionChangedAction::Reset);

    m_viewManager.ClearElement(element, isClearedDueToCollectionChange);
    m_viewportManager->OnElementCleared(element);
}

int ItemsRepeater::GetElementIndexImpl(const winrt::UIElement& element)
{
    auto virtInfo = TryGetVirtualizationInfo(element);
    return m_viewManager.GetElementIndex(virtInfo);
}

winrt::UIElement ItemsRepeater::GetElementFromIndexImpl(int index)
{
    winrt::UIElement result = nullptr;

    auto children = Children();
    for (unsigned i = 0u; i < children.Size() && !result; ++i)
    {
        auto element = children.GetAt(i);
        auto virtInfo = TryGetVirtualizationInfo(element);
        if (virtInfo && virtInfo->IsRealized() && virtInfo->Index() == index)
        {
            result = element;
        }
    }

    return result;
}

winrt::UIElement ItemsRepeater::GetOrCreateElementImpl(int index)
{
    if (index >= 0 && index >= ItemsSourceView().Count())
    {
        throw winrt::hresult_invalid_argument(L"Argument index is invalid.");
    }

    if (m_isLayoutInProgress)
    {
        throw winrt::hresult_error(E_FAIL, L"GetOrCreateElement invocation is not allowed during layout.");
    }

    auto element = GetElementFromIndexImpl(index);
    const bool isAnchorOutsideRealizedRange = !element;

    if (isAnchorOutsideRealizedRange)
    {
        if (!m_layout)
        {
            throw winrt::hresult_error(E_FAIL, L"Cannot make an Anchor when there is no attached layout.");
        }

        element = GetLayoutContext().GetOrCreateElementAt(index);
        element.Measure({ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() });
    }

    m_viewportManager->OnMakeAnchor(element, isAnchorOutsideRealizedRange);
    InvalidateMeasure();

    return element;
}

/*static*/
winrt::com_ptr<VirtualizationInfo> ItemsRepeater::TryGetVirtualizationInfo(const winrt::UIElement& element)
{
    auto value = element.GetValue(GetVirtualizationInfoProperty());
    return winrt::get_self<VirtualizationInfo>(value)->get_strong();
}

/*static*/
winrt::com_ptr<VirtualizationInfo> ItemsRepeater::GetVirtualizationInfo(const winrt::UIElement& element)
{
    auto result = TryGetVirtualizationInfo(element);

    if (!result)
    {
        result = CreateAndInitializeVirtualizationInfo(element);
    }

    return result;
}

/* static */
winrt::com_ptr<VirtualizationInfo> ItemsRepeater::CreateAndInitializeVirtualizationInfo(const winrt::UIElement& element)
{
    MUX_ASSERT(!TryGetVirtualizationInfo(element));
    auto result = winrt::make_self<VirtualizationInfo>();
    element.SetValue(GetVirtualizationInfoProperty(), result.as<winrt::IInspectable>());
    return result;
}

void ItemsRepeater::OnPropertyChanged(const winrt::DependencyPropertyChangedEventArgs& args)
{
    winrt::IDependencyProperty property = args.Property();

    if (property == s_itemsSourceProperty)
    {
        auto newValue = args.NewValue();
        auto newDataSource = safe_try_cast<winrt::ItemsSourceView>(newValue);
        if (newValue && !newDataSource)
        {
            newDataSource = winrt::ItemsSourceView(newValue);
        }

        OnDataSourcePropertyChanged(m_dataSource.get(), newDataSource);
    }
    else if (property == s_itemTemplateProperty)
    {
        OnItemTemplateChanged(safe_cast<winrt::IElementFactory>(args.OldValue()), safe_cast<winrt::IElementFactory>(args.NewValue()));
    }
    else if (property == s_layoutProperty)
    {
        OnLayoutChanged(safe_cast<winrt::VirtualizingLayout>(args.OldValue()), safe_cast<winrt::VirtualizingLayout>(args.NewValue()));
    }
    else if (property == s_animatorProperty)
    {
        OnAnimatorChanged(safe_cast<winrt::ElementAnimator>(args.OldValue()), safe_cast<winrt::ElementAnimator>(args.NewValue()));
    }
    else if (property == s_horizontalCacheLengthProperty)
    {
        m_viewportManager->HorizontalCacheLength(unbox_value<double>(args.NewValue()));
    }
    else if (property == s_verticalCacheLengthProperty)
    {
        m_viewportManager->VerticalCacheLength(unbox_value<double>(args.NewValue()));
    }
}

void ItemsRepeater::OnElementPrepared(const winrt::UIElement& element, int index)
{
    m_viewportManager->OnElementPrepared(element);
    if (m_elementPreparedEventSource)
    {
        if (!m_elementPreparedArgs)
        {
            m_elementPreparedArgs = tracker_ref<winrt::ItemsRepeaterElementPreparedEventArgs>(this, winrt::make<ItemsRepeaterElementPreparedEventArgs>(element, index));
        }
        else
        {
            winrt::get_self<ItemsRepeaterElementPreparedEventArgs>(m_elementPreparedArgs.get())->Update(element, index);
        }

        m_elementPreparedEventSource(*this, m_elementPreparedArgs.get());
    }
}

void ItemsRepeater::OnElementClearing(const winrt::UIElement& element)
{
    if (m_elementClearingEventSource)
    {
        if (!m_elementClearingArgs)
        {
            m_elementClearingArgs = tracker_ref<winrt::ItemsRepeaterElementClearingEventArgs>(this, winrt::make<ItemsRepeaterElementClearingEventArgs>(element));
        }
        else
        {
            winrt::get_self<ItemsRepeaterElementClearingEventArgs>(m_elementClearingArgs.get())->Update(element);
        }

        m_elementClearingEventSource(*this, m_elementClearingArgs.get());
    }
}

void ItemsRepeater::OnElementIndexChanged(const winrt::UIElement& element, int oldIndex, int newIndex)
{
    if (m_elementIndexChangedEventSource)
    {
        if (!m_elementIndexChangedArgs)
        {
            m_elementIndexChangedArgs = tracker_ref<winrt::ItemsRepeaterElementIndexChangedEventArgs>(this, winrt::make<ItemsRepeaterElementIndexChangedEventArgs>(element, oldIndex, newIndex));
        }
        else
        {
            winrt::get_self<ItemsRepeaterElementIndexChangedEventArgs>(m_elementIndexChangedArgs.get())->Update(element, oldIndex, newIndex);
        }

        m_elementIndexChangedEventSource(*this, m_elementIndexChangedArgs.get());
    }
}

void ItemsRepeater::OnLoaded(const winrt::IInspectable& /*sender*/, const winrt::RoutedEventArgs& /*args*/)
{
    // If we skipped an unload event, reset the scrollers now and invalidate measure so that we get a new
    // layout pass during which we will hookup new scrollers.
    if (_loadedCounter > _unloadedCounter)
    {
        InvalidateMeasure();
        m_viewportManager->ResetScrollers();
    }
    ++_loadedCounter;
}

void ItemsRepeater::OnUnloaded(const winrt::IInspectable& /*sender*/, const winrt::RoutedEventArgs& /*args*/)
{
    ++_unloadedCounter;
    // Only reset the scrollers if this unload event is in-sync.
    if (_unloadedCounter == _loadedCounter)
    {
        m_viewportManager->ResetScrollers();
    }
}

void ItemsRepeater::OnDataSourcePropertyChanged(const winrt::ItemsSourceView& oldValue, const winrt::ItemsSourceView& newValue)
{
    if (m_isLayoutInProgress)
    {
        throw winrt::hresult_error(E_FAIL, L"Cannot set ItemsSourceView during layout.");
    }

    m_dataSource.set(newValue);

    if (oldValue)
    {
        oldValue.CollectionChanged(m_dataSourceChanged);
    }

    if (newValue)
    {
        m_dataSourceChanged = newValue.CollectionChanged({ this, &ItemsRepeater::OnDataSourceChanged });
    }

    if (m_layout)
    {
        auto args = winrt::NotifyCollectionChangedEventArgs(
            winrt::NotifyCollectionChangedAction::Reset,
            nullptr /* newItems */,
            nullptr /* oldItems */,
            -1 /* newIndex */,
            -1 /* oldIndex */);
        args.Action();

        m_layout.OnItemsChangedCore(GetLayoutContext(), newValue, args);

        InvalidateMeasure();
    }
}

void ItemsRepeater::OnItemTemplateChanged(const winrt::IElementFactory&  oldValue, const winrt::IElementFactory&  newValue)
{
    if (m_isLayoutInProgress && oldValue)
    {
        throw winrt::hresult_error(E_FAIL, L"ItemTemplate cannot be changed during layout.");
    }

    m_itemTemplate = newValue;

#ifndef BUILD_WINDOWS
    m_itemTemplateWrapper = m_itemTemplate.try_as<winrt::IElementFactoryShim>();
    if (!m_itemTemplateWrapper)
    {
        // ItemTemplate set does not implement IElementFactoryShim. We also 
        // want to support DataTemplate and DataTemplateSelectors automagically.
        if (auto dataTemplate = m_itemTemplate.try_as<winrt::DataTemplate>())
        {
            m_itemTemplateWrapper = winrt::make<ItemTemplateWrapper>(dataTemplate);
        }
        else if (auto selector = m_itemTemplate.try_as<winrt::DataTemplateSelector>())
        {
            m_itemTemplateWrapper = winrt::make<ItemTemplateWrapper>(selector);
        }
        else
        {
            throw winrt::hresult_invalid_argument(L"ItemTemplate");
        }
    }
#endif
}

void ItemsRepeater::OnLayoutChanged(const winrt::VirtualizingLayout& oldValue, const winrt::VirtualizingLayout& newValue)
{
    if (m_isLayoutInProgress)
    {
        throw winrt::hresult_error(E_FAIL, L"Layout cannot be changed during layout.");
    }

    m_viewManager.OnLayoutChanging();
    m_animationManager.OnLayoutChanging();

    if (oldValue)
    {
        oldValue.UninitializeForContext(GetLayoutContext());
        oldValue.MeasureInvalidated(m_measureInvalidated);
        oldValue.ArrangeInvalidated(m_arrangeInvalidated);

        // Walk through all the elements and make sure they are cleared
        auto children = Children();
        for (unsigned i = 0u; i < children.Size(); ++i)
        {
            auto element = children.GetAt(i);
            if (GetVirtualizationInfo(element)->IsRealized())
            {
                ClearElementImpl(element);
            }
        }

        m_layoutState.set(nullptr);
    }

    m_layout = newValue;

    if (newValue)
    {
        newValue.InitializeForContext(GetLayoutContext());
        m_measureInvalidated = newValue.MeasureInvalidated({ this, &ItemsRepeater::InvalidateMeasureForLayout });
        m_arrangeInvalidated = newValue.ArrangeInvalidated({ this, &ItemsRepeater::InvalidateArrangeForLayout });
    }

    m_viewportManager->OnLayoutChanged();
    InvalidateMeasure();
}

void ItemsRepeater::OnAnimatorChanged(const winrt::ElementAnimator& /* oldValue */, const winrt::ElementAnimator& newValue)
{
    m_animator = newValue;
    m_animationManager.OnAnimatorChanged(newValue);
}

void ItemsRepeater::OnDataSourceChanged(const winrt::IInspectable& sender, const winrt::NotifyCollectionChangedEventArgs& args)
{
    if (m_isLayoutInProgress)
    {
        // Bad things will follow if the data changes while we are in the middle of a layout pass.
        throw winrt::hresult_error(E_FAIL, L"Changes in data source are not allowed during layout.");
    }

    if (IsProcessingCollectionChange())
    {
        throw winrt::hresult_error(E_FAIL, L"Changes in the data source are not allowed during another change in the data source.");
    }

    m_processingDataSourceChange.set(args);
    auto processingChange = gsl::finally([this]()
    {
        m_processingDataSourceChange.set(nullptr);
    });

    m_animationManager.OnDataSourceChanged(sender, args);
    m_viewManager.OnDataSourceChanged(sender, args);

    if (m_layout)
    {
        m_layout.OnItemsChangedCore(GetLayoutContext(), sender, args);
    }
}

void ItemsRepeater::InvalidateMeasureForLayout(winrt::Layout const&, winrt::IInspectable const&)
{
    InvalidateMeasure();
}

void ItemsRepeater::InvalidateArrangeForLayout(winrt::Layout const&, winrt::IInspectable const&)
{
    InvalidateArrange();
}

winrt::VirtualizingLayoutContext ItemsRepeater::GetLayoutContext()
{
    if (!m_layoutContext)
    {
        m_layoutContext.set(winrt::make<RepeaterLayoutContext>(*this));
    }
    return m_layoutContext.get();
}

winrt::IIterable<winrt::DependencyObject> ItemsRepeater::CreateChildrenInTabFocusOrderIterable()
{
    auto children = Children();
    if (children.Size() > 0u)
    {
        return winrt::make<ChildrenInTabFocusOrderIterable>(*this);
    }
    return nullptr;
}
