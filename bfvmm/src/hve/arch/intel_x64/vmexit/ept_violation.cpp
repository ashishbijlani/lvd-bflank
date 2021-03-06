//
// Copyright (C) 2019 Assured Information Security, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <hve/arch/intel_x64/vcpu.h>

namespace bfvmm::intel_x64
{

ept_violation_handler::ept_violation_handler(
    gsl::not_null<vcpu *> vcpu
) :
    m_vcpu{vcpu}
{
    using namespace vmcs_n;

    vcpu->add_handler(
        exit_reason::basic_exit_reason::ept_violation,
        ::handler_delegate_t::create<ept_violation_handler, &ept_violation_handler::handle>(this)
    );
}

// -----------------------------------------------------------------------------
// Add Handler / Enablers
// -----------------------------------------------------------------------------

void
ept_violation_handler::add_read_handler(
    const handler_delegate_t &d)
{ m_read_handlers.push_front(d); }

void
ept_violation_handler::add_write_handler(
    const handler_delegate_t &d)
{ m_write_handlers.push_front(d); }

void
ept_violation_handler::add_execute_handler(
    const handler_delegate_t &d)
{ m_execute_handlers.push_front(d); }

void
ept_violation_handler::set_default_read_handler(
    const ::handler_delegate_t &d)
{ m_default_read_handler = d; }

void
ept_violation_handler::set_default_write_handler(
    const ::handler_delegate_t &d)
{ m_default_write_handler = d; }

void
ept_violation_handler::set_default_execute_handler(
    const ::handler_delegate_t &d)
{ m_default_execute_handler = d; }

// -----------------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------------

bool
ept_violation_handler::handle(vcpu *vcpu)
{
    using namespace vmcs_n;
    auto qual = exit_qualification::ept_violation::get();

    struct info_t info = {
        guest_linear_address::get(),
        guest_physical_address::get(),
        qual,
        true
    };


    auto gva = guest_linear_address::get(); 
    auto gpa = guest_physical_address::get(); 

    bfdebug_transaction(0, [&](std::string * msg) {
        std::string ln = "EPT violation, guest linear:";
        bfn::to_string(ln, gva, 16);
        ln += ", guest physical:";
        bfn::to_string(ln, gpa, 16);

        ln += ", qualification:";
        bfn::to_string(ln, qual, 16);

        ln += " [ ";

        auto qual_string = [&]() {
            std::string _qual;
            for (auto b = 0; b < 8; b++) {
                if ((qual >> b) & 0x1) {
                    switch (b) {
                    case 0:
                        _qual += " data read, ";
                        break;
                    case 1:
                        _qual += " data write, ";
                        break;
                    case 2:
                        _qual += " ifetch, ";
                        break;
                    case 3:
                        _qual += " read access violation, ";
                        break;
                    case 4:
                        _qual += " write access violation, ";
                        break;
                    case 5:
                        _qual += " execute access violation, ";
                        break;
                    case 7:
                        _qual += " guest linear addr valid ";
                        break;
                    }
                }
            }
            return _qual;
        }();
        ln += qual_string;
        ln += " ] ";
        bfdebug_info(0, ln.c_str(), msg);
    });

    vcpu->dump("Guest CPU state");

    vcpu->dump_ept_entry(gpa); 
    
    //std::pair<uintptr_t, uintptr_t> gpa_hpa_pair = vcpu->gpa_to_hpa(gpa);
    //
    //bfdebug_transaction(0, [&](std::string * msg) {
    //       bferror_subnhex(0, "gpa", gpa_hpa_pair.first, msg);
    //        bferror_subnhex(0, "hpa", gpa_hpa_pair.second, msg);
    //});

    //vcpu->dump_stack(); 

    if (exit_qualification::ept_violation::data_read::is_enabled(qual)) {
        return handle_read(vcpu, info);
    }

    if (exit_qualification::ept_violation::data_write::is_enabled(qual)) {
        return handle_write(vcpu, info);
    }

    if (exit_qualification::ept_violation::instruction_fetch::is_enabled(qual)) {
        return handle_execute(vcpu, info);
    }

    throw std::runtime_error(
        "ept_violation_handler::handle: unhandled ept violation"
    );
}

bool
ept_violation_handler::handle_read(vcpu *vcpu, info_t &info)
{
    for (const auto &d : m_read_handlers) {
        if (d(vcpu, info)) {

            if (!info.ignore_advance) {
                return vcpu->advance();
            }

            return true;
        }
    }

    if (m_default_read_handler.is_valid()) {
        return m_default_read_handler(vcpu);
    }

    throw std::runtime_error(
        "ept_violation_handler: unhandled ept read violation"
    );
}

bool
ept_violation_handler::handle_write(vcpu *vcpu, info_t &info)
{
    for (const auto &d : m_write_handlers) {
        if (d(vcpu, info)) {

            if (!info.ignore_advance) {
                return vcpu->advance();
            }

            return true;
        }
    }

    if (m_default_write_handler.is_valid()) {
        return m_default_write_handler(vcpu);
    }

    throw std::runtime_error(
        "ept_violation_handler: unhandled ept write violation"
    );
}

bool
ept_violation_handler::handle_execute(vcpu *vcpu, info_t &info)
{
    for (const auto &d : m_execute_handlers) {
        if (d(vcpu, info)) {

            if (!info.ignore_advance) {
                return vcpu->advance();
            }

            return true;
        }
    }

    if (m_default_execute_handler.is_valid()) {
        return m_default_execute_handler(vcpu);
    }

    throw std::runtime_error(
        "ept_violation_handler: unhandled ept execute violation"
    );
}

}
