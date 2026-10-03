# HCC2D Terms of Service

English reference copy. The current web version is available at
[https://hcc2d.com/en/terms](https://hcc2d.com/en/terms).

Repository notice: HCC2D Streamer CLI is a separate open-source project and is
not a Covered Product under these Terms. Its use, copying, modification, and
distribution are governed by the [Apache License 2.0](LICENSE). These Terms do
not replace or restrict the rights granted by that license. Section 6 provides
safety information relevant to the CLI's visual-streaming output.

Last updated: October 3, 2026

## 1. Applications and Services Covered

These terms apply to the following applications and services for encoding and
decoding QR and HCC2D codes:

- The HCC2D Decoder Android application
- The HCC2D Decoder iOS application
- The HCC2D Encoder macOS application
- The HCC2D Streamer macOS application
- The HCC2D Streamer Windows application
- The HCC2D website at hcc2d.com, including the web-based barcode generator
  and REST API

The applications and services in this list are referred to collectively in
these Terms as the "Covered Products."

### Open-Source CLI Tools

The following command-line tools are separate open-source projects. They are
not Covered Products and are not included in the term "applications" as used
in these Terms:

- HCC2D Encoder CLI for Linux, distributed through GitHub and the official
  HCC2D Encoder APT repository
- HCC2D Streamer CLI for Linux, distributed through GitHub and the official
  HCC2D Streamer APT repository

The use, copying, modification, and distribution of each CLI tool are governed
by its respective open-source license. These Terms do not replace or restrict
the rights granted by those licenses. The safety information in Section 6 also
describes the visual-streaming output of HCC2D Streamer CLI where applicable;
including that information does not make the CLI a Covered Product.

The Covered Products are developed and maintained by Querini Marco, Rome,
Italy (hereinafter referred to as "we", "us", or "the developer").

The HCC2D Encoder, HCC2D Streamer, and HCC2D Decoder applications included
among the Covered Products, along with the HCC2D website, originate from a
personal project developed in the developer's spare time and are provided free
of charge for educational and experimental purposes, without any commercial
or monetization intent. For this reason, the applications contain no
advertising, in-app purchases, or other forms of monetization, and the website
does not display advertising or require payment.

## 2. Acceptance of Terms

By installing, accessing, or using any Covered Product, you confirm that you
have read, understood, and agree to these Terms of Service. If you do not
agree, you must discontinue use immediately.

If you are using an application on behalf of an organization, you represent
that you are authorized to bind that organization to these terms.

## 3. HCC2D Decoder Applications (Android and iOS)

The HCC2D Decoder apps use your device camera to scan and decode static QR and
HCC2D codes. They can also receive and decode animated visual streams made up
of successive QR or HCC2D symbols and reassemble the transmitted data into a
file.

**Decoded Content.** We are not responsible for the content encoded in codes
you scan. Decoded data — including URLs, text, and structured payloads — may
originate from unknown third parties. You are solely responsible for how you
act on decoded information. We strongly recommend caution when opening links
or acting on content from unfamiliar or untrusted sources.

**Camera Access.** The apps request camera permission to perform live scanning.
Camera frames are processed locally on your device and are not transmitted to
our servers. Users who decline camera permission can still use the app by
uploading images through the image upload function.

**Scan History.** Scan history is stored locally on your device and is not
uploaded to our servers. You may clear your scan history at any time through
your device settings.

## 4. HCC2D Encoder for Mac

The HCC2D Encoder is a macOS application that allows you to create QR codes and
HCC2D barcode images, manage encoding projects, and maintain a local export
history.

**Local Processing.** The App operates locally on your Mac. Project files, text
payloads, imported file references, export settings, and generated outputs are
processed on your device and are not transmitted to our servers as part of the
App's normal workflows.

**Generated Codes.** You are responsible for the content you choose to encode.
You agree not to use the App to encode illegal, harmful, fraudulent, or
misleading content. We are not responsible for how generated barcode images
are used, distributed, or interpreted by third parties after they leave your
device.

**Project Files and Local Data.** Project files and export history are stored
locally on your Mac under directories you control. You are responsible for
managing, backing up, and securing those files.

**Mac App Store.** When distributed through the Apple Mac App Store, your
download is also subject to Apple's Terms and Conditions.

## 5. HCC2D Streamer Applications (macOS and Windows)

The HCC2D Streamer is a desktop application, available for macOS and Windows,
that allows you to select or drag a file, encode it as a sequence of QR or
HCC2D barcode images, and display the resulting frames on screen for local
optical transfer to a receiving device.

**Local Processing.** The applications operate locally on your Mac or Windows
PC. File content selected by you, clipboard content explicitly requested by
you, streaming settings, and generated encoded data are processed on your
device and are not transmitted to our servers as part of normal streaming
workflows.

**Generated Codes.** You are responsible for the content you choose to encode
and stream. You agree not to use the applications to encode or transmit
illegal, harmful, fraudulent, or misleading content. We are not responsible
for how streamed barcode frames are captured, decoded, or used by any receiving
device or third party.

**Local Data.** Security-scoped bookmarks (on macOS), language preferences, and
other limited local app state are stored locally on your Mac or Windows PC. You
are responsible for managing and securing files on your device.

**Mac App Store.** When distributed through the Apple Mac App Store, your
download is also subject to Apple's Terms and Conditions.

## 6. Photosensitivity and Visual Streaming

HCC2D Decoder can decode both QR and HCC2D codes, whether they are presented as
a single static image or, for file transfer, as an animated visual stream.
During file transfer, the HCC2D Streamer applications or HCC2D Streamer CLI
display a sequence of QR or HCC2D symbols that HCC2D Decoder decodes and
reassembles into the original file.

### Static Codes

Static QR and HCC2D codes are displayed as a single, non-animated image. They
do not produce the rapid sequence of visual changes generated by visual
streaming.

### Visual Streaming and Flashing Patterns

The visual streaming functionality of the HCC2D Streamer applications and
HCC2D Streamer CLI transfers file data by displaying a sequence of changing QR
or HCC2D symbols. Depending on the selected configuration and transmission
speed, it may cause rapid changes in color or brightness, high-contrast
patterns, flickering, or flashing visual effects.

These visual effects may cause discomfort or adverse reactions in individuals
who are sensitive to flashing, flickering, or rapidly changing visual patterns,
including individuals with photosensitive epilepsy.

Individuals who have photosensitive epilepsy, have previously experienced
adverse reactions to flashing or flickering images, are sensitive to rapidly
changing visual patterns, or have been advised to avoid such stimuli should not
use the visual streaming functionality.

Users should immediately stop viewing the display and discontinue use of the
visual streaming functionality if they experience discomfort or any adverse
symptoms.

### Reduced-Rate Modes

The HCC2D Streamer applications and HCC2D Streamer CLI may provide a mode
limited to no more than three visual symbols per second, as well as other
lower-rate or reduced-flashing options intended to reduce the frequency of
visual changes.

The three-symbol-per-second limit is a conservative precaution informed by the
World Wide Web Consortium (W3C) Web Content Accessibility Guidelines (WCAG)
2.2, Success Criterion 2.3.1, "Three Flashes or Below Threshold." This criterion
addresses content that flashes more than three times in any one-second period,
unless the flashing remains below the defined general-flash and red-flash
thresholds. In this mode, the symbol sequence itself is updated no more than
three times per second. A symbol change is not necessarily a "flash" as that
term is defined by WCAG. This conservative limit reduces the frequency of
visual changes but does not, by itself, establish WCAG conformance or guarantee
medical safety.

Such options are provided as precautionary features and must not be interpreted
as a guarantee of medical safety or as a representation that adverse reactions
cannot occur.

Users with known or suspected photosensitivity should avoid the visual
streaming functionality rather than relying on a reduced frame-rate setting.

### High-Speed Streaming

Higher-speed streaming modes may display more than three QR or HCC2D symbols
per second and may therefore produce more than three visual changes per second.

Where applicable, the HCC2D Streamer applications or HCC2D Streamer CLI may
display a warning before activating high-speed streaming.

Users should ensure that other individuals who may be sensitive to flashing or
flickering visual content are not inadvertently exposed to the visual
streaming display.

### User Responsibility and Limitation of Liability

Users are responsible for selecting and using HCC2D functionality in a manner
appropriate to their circumstances and for observing the warnings and
precautions provided with the software.

For HCC2D Streamer CLI, this section provides safety information only and does
not modify or replace its open-source license.

With respect to the Covered Products, and to the maximum extent permitted by
applicable law, we make no representation or warranty that the visual streaming
functionality is suitable for every individual or for any specific medical
condition or health circumstance.

With respect to the Covered Products, and to the maximum extent permitted by
applicable law, we and the relevant developers, contributors, operators, and
distributors shall not be liable for losses, damages, or adverse effects arising
from the use or misuse of the visual streaming functionality, except where such
liability cannot lawfully be excluded or limited.

Nothing in these Terms is intended to exclude, restrict, or limit any rights,
remedies, warranties, or liabilities that cannot be excluded, restricted, or
limited under applicable law.

## 7. HCC2D Website and Web Generator

The HCC2D website provides a web-based barcode generator and informational
content about the HCC2D standard.

**Generated Codes.** You agree not to use the web generator to encode illegal,
harmful, fraudulent, or misleading content. We are not responsible for how
generated barcode images are used, distributed, or interpreted by third
parties.

**Image Retention.** Images generated through the website are retained on our
servers for up to one hour. You are responsible for downloading and storing any
images you need within that timeframe.

**Website Availability.** We do not guarantee that the website will be
available at all times or free of errors. We may suspend or discontinue any
part of the website at any time without prior notice.

## 8. API Access

Access to the HCC2D REST API requires an API key obtained through the official
request form. Keys are issued at our discretion. The following conditions
apply:

- API keys are personal and non-transferable; you must not share, sell, or
  redistribute your key.
- Usage is subject to rate limits: 30 requests per minute per key, 60 per
  minute per IP address.
- Repeated failed authentication attempts will result in a temporary IP
  lockout.
- We reserve the right to revoke keys misused or used in violation of these
  terms, without prior notice.
- You must not use automated tools to circumvent rate limits or abuse the key
  request process.
- Generated images are retained on the server for up to 1 hour; download them
  within that window.

## 9. Acceptable Use

You agree to use the Covered Products only for lawful purposes and in a manner
that does not infringe the rights of others. In particular, you must not:

- Encode, distribute, or process illegal, harmful, fraudulent, or misleading
  content through any Covered Product
- Attempt to gain unauthorized access to any part of the services or
  infrastructure
- Interfere with or disrupt the operation of the website, API, or related
  systems
- Reverse-engineer or decompile any covered HCC2D application except as
  permitted by applicable law
- Use any Covered Product in a way that violates applicable local, national, or
  international laws

## 10. Intellectual Property

The Covered Products and their software implementations are protected by
applicable intellectual property laws.

Certain methods, concepts, and elements implemented within HCC2D are derived
from or related to academic research authored or co-authored by the developer.
Rights relating to published papers, research articles, specifications, and
associated scholarly materials remain subject to the rights of their respective
authors, institutions, and publishers.

These Terms do not restrict academic reference, discussion, citation, or
independent implementation of publicly available HCC2D research or
specifications. No ownership claim is made over publicly available academic
research, specifications, or independent implementations of HCC2D. However,
these Terms do not grant any right to claim official affiliation with the HCC2D
Encoder, HCC2D Streamer, and Decoder applications distributed by the developer
through application stores without prior written permission.

Content created by users through the Covered Products, including project files,
encoded data, and exported barcode images, remains the property of the
respective user.

The software implementation of the HCC2D Encoder, HCC2D Streamer, and HCC2D
Decoder applications distributed by the developer remains the property of the
developer.

## 11. No Warranty

The Covered Products are provided "as is" and "as available", without warranty
of any kind, express or implied, including but not limited to warranties of
merchantability, fitness for a particular purpose, accuracy, or
non-infringement.

We do not warrant that the apps or website will be available at all times or
free of errors, that generated barcodes will be readable by any specific
scanner, or that decoded content will be accurate.

## 12. Limitation of Liability

To the fullest extent permitted by applicable law, we shall not be liable for
any direct, indirect, incidental, special, or consequential damages arising
from your use of any Covered Product. This includes damages from decoded
content from unknown sources, from generated codes used by third parties, from
loss of project data or locally stored files, from service interruptions, or
from errors in barcode data.

Some jurisdictions do not allow the exclusion or limitation of certain types
of liability. In such cases, our liability is limited to the maximum extent
permitted by applicable law.

## 13. Privacy

Where applicable, your use of a Covered Product is also governed by the
relevant Privacy Policy, which describes how we handle information in
connection with that product. By using a product for which a Privacy Policy is
provided, you acknowledge that you have read and understood that policy.

## 14. Changes to These Terms

We may update these Terms of Service at any time. The updated version will be
published on this page with a revised date.

For the HCC2D Encoder macOS app, the HCC2D Streamer macOS and Windows apps, and
the HCC2D Decoder iOS and Android apps, the version of these terms current at
the time of each release is bundled offline within the app. Updated terms will
be included in future app releases.

Continued use of any Covered Product after changes are posted constitutes your
acceptance of the revised terms.

## 15. Governing Law

These terms are governed by the laws of Italy and applicable European Union
regulations, without regard to conflict of law provisions, except where
mandatory local consumer protection laws of your jurisdiction apply.

## 16. Contact

For questions about these terms, contact us at info@hcc2d.com.
