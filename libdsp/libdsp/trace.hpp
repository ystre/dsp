/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Trace (OpenTelemetry)
 *
 */

#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <fmt/format.h>

#include <memory>
#include <string>
#include <string_view>

namespace dsp {

class tracer {
public:
    tracer(const std::string& address) {
        opentelemetry::sdk::trace::BatchSpanProcessorOptions bspOpts{ };
        auto opts = opentelemetry::exporter::otlp::OtlpHttpExporterOptions{ };
        opts.url = address;

        auto exporter = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(opts);
        auto processor = opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(std::move(exporter), bspOpts);
        m_provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(std::move(processor));
        trace_api::Provider::SetTracerProvider(m_provider);
        m_tracer = m_provider->GetTracer("dsp", "1.0.0");
    }

    auto start(std::string_view name) -> opentelemetry::trace::Scope {
        auto span = m_tracer->StartSpan(name);
        return m_tracer->WithActiveSpan(span);
    }

private:
    std::shared_ptr<opentelemetry::trace::TracerProvider> m_provider;
    std::shared_ptr<opentelemetry::trace::Tracer> m_tracer;
};

} // namespace dsp
