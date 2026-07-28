// Qt image-codec boundary for the toolkit-agnostic thumbnail service.
#pragma once

#include "odysea/core/thumbnail_service.hpp"

#include <QImage>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>

struct ThumbnailDecodeLimits {
    std::uint32_t max_dimension = 16'384;
    std::size_t max_decoded_bytes = 256UL * 1024UL * 1024UL;
};

[[nodiscard]] QImage thumbnailImageToQImage(const odysea::core::ThumbnailImage& source,
                                            std::error_code& error);

[[nodiscard]] odysea::core::ThumbnailImage qImageToThumbnailImage(const QImage& source,
                                                                  std::error_code& error);

class QtThumbnailProducer final : public odysea::core::ThumbnailProducer {
  public:
    explicit QtThumbnailProducer(ThumbnailDecodeLimits limits = {});

    [[nodiscard]] odysea::core::ThumbnailImage produce(const std::filesystem::path& source,
                                                       odysea::core::ThumbnailSize size,
                                                       std::error_code& error) override;

  private:
    ThumbnailDecodeLimits limits_;
};

class FreedesktopThumbnailStore final : public odysea::core::ThumbnailStore {
  public:
    explicit FreedesktopThumbnailStore(ThumbnailDecodeLimits limits = {});

    [[nodiscard]] std::optional<odysea::core::StoredThumbnail>
    load(const odysea::core::ThumbnailKey& key, std::error_code& error) override;

    void save(const odysea::core::ThumbnailKey& key, const odysea::core::ThumbnailImage& image,
              std::error_code& error) override;

  private:
    ThumbnailDecodeLimits limits_;
};
