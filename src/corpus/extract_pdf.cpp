#include "corpus/extract_pdf.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <fpdf_text.h>
#include <fpdfview.h>

#include "corpus/extract_html.hpp"

namespace vigie::corpus {
namespace {

class PdfiumLibrary {
public:
    PdfiumLibrary() { FPDF_InitLibrary(); }
    ~PdfiumLibrary() { FPDF_DestroyLibrary(); }
    PdfiumLibrary(const PdfiumLibrary&) = delete;
    PdfiumLibrary& operator=(const PdfiumLibrary&) = delete;
};

struct DocumentDeleter {
    void operator()(fpdf_document_t__* document) const { FPDF_CloseDocument(document); }
};
struct PageDeleter {
    void operator()(fpdf_page_t__* page) const { FPDF_ClosePage(page); }
};
struct TextPageDeleter {
    void operator()(fpdf_textpage_t__* page) const { FPDFText_ClosePage(page); }
};

std::string utf16_to_utf8(const std::u16string& text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t code = text[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size()) {
            const std::uint32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }

        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    return out;
}

std::string read_page_text(FPDF_DOCUMENT document, int index) {
    const std::unique_ptr<fpdf_page_t__, PageDeleter> page(FPDF_LoadPage(document, index));
    if (!page) {
        throw std::runtime_error("Page PDF illisible");
    }

    const std::unique_ptr<fpdf_textpage_t__, TextPageDeleter> text_page(FPDFText_LoadPage(page.get()));
    if (!text_page) {
        throw std::runtime_error("Texte de page PDF illisible");
    }

    const int count = FPDFText_CountChars(text_page.get());
    if (count <= 0) {
        return {};
    }

    std::u16string buffer(static_cast<std::size_t>(count) + 1, u'\0');
    const int written = FPDFText_GetText(text_page.get(), 0, count,
                                         reinterpret_cast<unsigned short*>(buffer.data()));
    buffer.resize(written > 0 ? static_cast<std::size_t>(written) - 1 : 0);
    return utf16_to_utf8(buffer);
}

}  // namespace

std::vector<Paragraph> extract_pdf(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Fichier introuvable : " + path.string());
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const PdfiumLibrary library;
    const std::unique_ptr<fpdf_document_t__, DocumentDeleter> document(
        FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr));
    if (!document) {
        throw std::runtime_error("PDF illisible : " + path.string());
    }

    std::vector<Paragraph> paragraphs;
    const int pages = FPDF_GetPageCount(document.get());
    for (int index = 0; index < pages; ++index) {
        std::string text = clean_text(read_page_text(document.get(), index));
        if (!text.empty()) {
            const int number = index + 1;
            paragraphs.push_back({"Page " + std::to_string(number), number, std::move(text)});
        }
    }

    return paragraphs;
}

}  // namespace vigie::corpus
