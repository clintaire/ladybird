/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/RefCounted.h>
#include <LibCore/Socket.h>
#include <LibURL/URL.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/Fetching.h>
#include <LibWeb/HTML/UniversalGlobalScope.h>
#include <LibWeb/HTML/WindowOrWorkerGlobalScope.h>
#include <LibWeb/HTML/WorkerLocation.h>
#include <LibWeb/HTML/WorkerNavigator.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

#define ENUMERATE_WORKER_GLOBAL_SCOPE_EVENT_HANDLERS(E)       \
    E(onerror, HTML::EventNames::error)                       \
    E(onlanguagechange, HTML::EventNames::languagechange)     \
    E(ononline, HTML::EventNames::online)                     \
    E(onoffline, HTML::EventNames::offline)                   \
    E(onrejectionhandled, HTML::EventNames::rejectionhandled) \
    E(onunhandledrejection, HTML::EventNames::unhandledrejection)

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/workers.html#the-workerglobalscope-common-interface
// WorkerGlobalScope is the base class of each real WorkerGlobalScope that will be created when the
// user agent runs the run a worker algorithm.
class WorkerGlobalScope
    : public DOM::EventTarget
    , public WindowOrWorkerGlobalScopeMixin
    , public UniversalGlobalScopeMixin {
    WEB_PLATFORM_OBJECT(WorkerGlobalScope, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(WorkerGlobalScope);

public:
    virtual ~WorkerGlobalScope() override;

    // ^WindowOrWorkerGlobalScopeMixin
    virtual DOM::EventTarget& this_impl() override { return *this; }
    virtual DOM::EventTarget const& this_impl() const override { return *this; }

    using UniversalGlobalScopeMixin::atob;
    using UniversalGlobalScopeMixin::btoa;
    using UniversalGlobalScopeMixin::queue_microtask;
    using UniversalGlobalScopeMixin::structured_clone;
    using WindowOrWorkerGlobalScopeMixin::clear_interval;
    using WindowOrWorkerGlobalScopeMixin::clear_timeout;
    using WindowOrWorkerGlobalScopeMixin::create_image_bitmap;
    using WindowOrWorkerGlobalScopeMixin::fetch;
    using WindowOrWorkerGlobalScopeMixin::performance;
    using WindowOrWorkerGlobalScopeMixin::set_interval;
    using WindowOrWorkerGlobalScopeMixin::set_timeout;

    // Following methods are from the WorkerGlobalScope IDL definition
    // https://html.spec.whatwg.org/multipage/workers.html#the-workerglobalscope-common-interface

    // https://html.spec.whatwg.org/multipage/workers.html#dom-workerglobalscope-self
    GC::Ref<WorkerGlobalScope const> self() const { return *this; }

    GC::Ref<WorkerLocation> location() const;
    GC::Ref<WorkerNavigator> navigator() const;
    WebIDL::ExceptionOr<void> import_scripts(Vector<String> const& urls, PerformTheFetchHook = nullptr);

#undef __ENUMERATE
#define __ENUMERATE(attribute_name, event_name)       \
    void set_##attribute_name(WebIDL::CallbackType*); \
    WebIDL::CallbackType* attribute_name();
    ENUMERATE_WORKER_GLOBAL_SCOPE_EVENT_HANDLERS(__ENUMERATE)
#undef __ENUMERATE

    GC::Ref<CSS::FontFaceSet> fonts();

    // Non-IDL public methods

    URL::URL const& url() const { return m_url.value(); }
    void set_url(URL::URL const& url) { m_url = url; }

    String const& name() const { return m_name; }
    void set_name(String name) { m_name = move(name); }

    Bindings::WorkerType type() const { return m_type; }
    void set_type(Bindings::WorkerType type) { m_type = type; }

    // Spec note: While the WorkerLocation object is created after the WorkerGlobalScope object,
    //            this is not problematic as it cannot be observed from script.
    void set_location(GC::Ref<WorkerLocation> loc) { m_location = move(loc); }

    void set_internal_port(GC::Ref<MessagePort> port);

    void initialize_web_interfaces(Badge<WorkerEnvironmentSettingsObject>) { initialize_web_interfaces_impl(); }

    Web::Page* page() { return m_page.ptr(); }

    GC::Ref<PolicyContainer> policy_container() const;

    bool is_closing() const { return m_closing; }

    void initialize_policy_container(GC::Ref<Fetch::Infrastructure::Response const> response, GC::Ref<EnvironmentSettingsObject> environment);
    [[nodiscard]] ContentSecurityPolicy::Directives::Directive::Result run_csp_initialization() const;

protected:
    explicit WorkerGlobalScope(JS::Realm&, GC::Ref<Web::Page>);

    virtual void initialize_web_interfaces_impl();

    void close_a_worker();

    virtual void finalize() override;

    GC::Ptr<MessagePort> m_internal_port;

private:
    virtual bool is_window_or_worker_global_scope_mixin() const final { return true; }

    virtual void visit_edges(Cell::Visitor&) override;

    GC::Ptr<WorkerLocation> m_location;
    GC::Ptr<WorkerNavigator> m_navigator;

    GC::Ref<Web::Page> m_page;

    // https://html.spec.whatwg.org/multipage/workers.html#concept-WorkerGlobalScope-owner-set
    // FIXME: A WorkerGlobalScope object has an associated owner set (a set of Document and WorkerGlobalScope objects). It is initially empty and populated when the worker is created or obtained.
    //     Note: It is a set, instead of a single owner, to accommodate SharedWorkerGlobalScope objects.

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-type
    // A WorkerGlobalScope object has an associated type ("classic" or "module"). It is set during creation.
    Bindings::WorkerType m_type { Bindings::WorkerType::Classic };

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-url
    // A WorkerGlobalScope object has an associated url (null or a URL). It is initially null.
    Optional<URL::URL> m_url;

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-name
    // A WorkerGlobalScope object has an associated name (a string). It is set during creation.
    //  Note: The name can have different semantics for each subclass of WorkerGlobalScope.
    //        For DedicatedWorkerGlobalScope instances, it is simply a developer-supplied name, useful mostly for debugging purposes.
    //        For SharedWorkerGlobalScope instances, it allows obtaining a reference to a common shared worker via the SharedWorker() constructor.
    //        For ServiceWorkerGlobalScope objects, it doesn't make sense (and as such isn't exposed through the JavaScript API at all).
    String m_name;

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-policy-container
    // A WorkerGlobalScope object has an associated policy container (a policy container). It is initially a new policy container.
    mutable GC::Ptr<PolicyContainer> m_policy_container;

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-embedder-policy
    // A WorkerGlobalScope object has an associated embedder policy (an embedder policy).
    // FIXME: Should be removed, https://github.com/whatwg/html/issues/11316

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-module-map
    // FIXME: A WorkerGlobalScope object has an associated module map. It is a module map, initially empty.

    // https://html.spec.whatwg.org/multipage/workers.html#concept-workerglobalscope-cross-origin-isolated-capability
    bool m_cross_origin_isolated_capability { false };

    // https://html.spec.whatwg.org/multipage/workers.html#dom-workerglobalscope-closing
    bool m_closing { false };

    // https://drafts.csswg.org/css-font-loading/#font-source
    GC::Ptr<CSS::FontFaceSet> m_fonts;
};

}
