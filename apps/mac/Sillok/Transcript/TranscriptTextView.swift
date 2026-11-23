import AppKit
import SillokEngine
import SwiftUI

struct TranscriptTextView: NSViewRepresentable {
    let document: TranscriptDocument
    let player: Player
    let search: String
    let searchStep: Int
    let current: Int?
    @Binding var following: Bool
    let play: (Int) -> Void
    let edit: (Int) -> Void
    let change: (() throws -> Void) -> Void

    static let paragraphPause = 2000

    static func matches(_ search: String, in text: String) -> Bool {
        text.range(of: search, options: [.caseInsensitive, .diacriticInsensitive]) != nil
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeNSView(context: Context) -> NSScrollView {
        let textView = SentenceTextView(usingTextLayoutManager: false)
        textView.coordinator = context.coordinator
        textView.isEditable = false
        textView.drawsBackground = false
        textView.textContainerInset = NSSize(width: 20, height: 12)
        textView.isVerticallyResizable = true
        textView.autoresizingMask = .width
        textView.textContainer?.widthTracksTextView = true
        let scrollView = TranscriptScrollView()
        scrollView.userScrolled = context.coordinator.userScrolled
        scrollView.documentView = textView
        scrollView.hasVerticalScroller = true
        scrollView.drawsBackground = false
        NotificationCenter.default.addObserver(context.coordinator, selector: #selector(Coordinator.userScrolled),
                                               name: NSScrollView.willStartLiveScrollNotification, object: scrollView)
        context.coordinator.textView = textView
        return scrollView
    }

    func updateNSView(_ scrollView: NSScrollView, context: Context) {
        context.coordinator.parent = self
        context.coordinator.update()
    }

    @MainActor
    final class Coordinator: NSObject {
        var parent: TranscriptTextView?
        weak var textView: NSTextView?
        private var sentences: [NSRange] = []
        private var shown: (transcript: Transcript, edits: TranscriptEdits)?
        private var shownSearch = ""
        private var shownStep = 0
        private var shownCurrent: Int?
        private var wasFollowing = true

        @objc func userScrolled() {
            parent?.following = false
        }

        func update() {
            guard let parent, let textView, let storage = textView.textStorage else { return }
            let document = parent.document
            let rebuilt = shown?.transcript != document.transcript || shown?.edits != document.edits
            if rebuilt {
                let (text, sentences) = TranscriptText.make(document)
                storage.setAttributedString(text)
                self.sentences = sentences
                shown = (document.transcript, document.edits)
            }
            if rebuilt || parent.search != shownSearch || parent.current != shownCurrent {
                highlight(search: parent.search, current: parent.current)
            }
            if parent.search != shownSearch || parent.searchStep != shownStep {
                let found = matches(parent.search)
                if !found.isEmpty { scroll(to: found[parent.searchStep % found.count], onlyIfHidden: false) }
            }
            if parent.following, let current = parent.current, sentences.indices.contains(current),
               current != shownCurrent || !wasFollowing {
                scroll(to: sentences[current], onlyIfHidden: wasFollowing)
            }
            shownSearch = parent.search
            shownStep = parent.searchStep
            shownCurrent = parent.current
            wasFollowing = parent.following
        }

        func sentence(at point: NSPoint) -> Int? {
            guard let textView, let layoutManager = textView.layoutManager, let container = textView.textContainer,
                  let storage = textView.textStorage, storage.length > 0
            else { return nil }
            let location = NSPoint(x: point.x - textView.textContainerOrigin.x, y: point.y - textView.textContainerOrigin.y)
            let index = layoutManager.characterIndex(for: location, in: container, fractionOfDistanceBetweenInsertionPoints: nil)
            return storage.attribute(.sentence, at: min(index, storage.length - 1), effectiveRange: nil) as? Int
        }

        func menu(for index: Int, copyingSelection: Bool) -> NSMenu {
            let menu = NSMenu()
            guard let parent else { return menu }
            let document = parent.document
            let segment = document.transcript.segments[index]
            if copyingSelection {
                menu.addItem(withTitle: String(localized: "Copy"), action: #selector(NSText.copy(_:)), keyEquivalent: "")
            } else {
                menu.addItem(ActionItem(String(localized: "Copy")) {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(segment.text, forType: .string)
                })
            }
            menu.addItem(ActionItem(String(localized: "Play from Here")) { parent.play(index) })
            menu.addItem(.separator())
            menu.addItem(ActionItem(String(localized: "Edit Text…")) { parent.edit(index) })
            if !document.transcript.speakers.isEmpty {
                let speakers = NSMenu()
                for speaker in document.transcript.speakers {
                    let name = Speakers.name(speaker.id, in: document.transcript)
                    speakers.addItem(ActionItem(name, checked: speaker.id == segment.speaker) {
                        parent.change { try document.setSpeaker(speaker.id, of: index) }
                    })
                }
                let item = NSMenuItem(title: String(localized: "Speaker"), action: nil, keyEquivalent: "")
                item.submenu = speakers
                menu.addItem(item)
            }
            if document.edits.isEdited(index) {
                menu.addItem(ActionItem(String(localized: "Restore the Original")) {
                    parent.change { try document.restore(index) }
                })
            }
            return menu
        }

        private func matches(_ search: String) -> [NSRange] {
            guard !search.isEmpty, let text = textView?.textStorage?.string as NSString? else { return [] }
            return sentences.flatMap { sentence in
                var found: [NSRange] = []
                var rest = sentence
                while case let range = text.range(of: search, options: [.caseInsensitive, .diacriticInsensitive], range: rest),
                      range.location != NSNotFound {
                    found.append(range)
                    rest = NSRange(location: NSMaxRange(range), length: NSMaxRange(sentence) - NSMaxRange(range))
                }
                return found
            }
        }

        private func highlight(search: String, current: Int?) {
            guard let layoutManager = textView?.layoutManager, let length = textView?.textStorage?.length else { return }
            let all = NSRange(location: 0, length: length)
            layoutManager.removeTemporaryAttribute(.backgroundColor, forCharacterRange: all)
            if let current, sentences.indices.contains(current) {
                layoutManager.addTemporaryAttribute(.backgroundColor, value: NSColor.controlAccentColor.withAlphaComponent(0.18),
                                                    forCharacterRange: sentences[current])
            }
            for range in matches(search) {
                layoutManager.addTemporaryAttribute(.backgroundColor, value: NSColor.systemYellow.withAlphaComponent(0.45),
                                                    forCharacterRange: range)
            }
        }

        private func scroll(to range: NSRange, onlyIfHidden: Bool) {
            guard let textView, let layoutManager = textView.layoutManager, let container = textView.textContainer,
                  let scrollView = textView.enclosingScrollView
            else { return }
            let glyphs = layoutManager.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
            let rect = layoutManager.boundingRect(forGlyphRange: glyphs, in: container)
                .offsetBy(dx: 0, dy: textView.textContainerOrigin.y)
            let visible = scrollView.contentView.documentVisibleRect
            if onlyIfHidden, rect.minY >= visible.minY, rect.maxY <= visible.maxY - 40 { return }
            let top = min(max(rect.midY - visible.height / 2, 0), max(textView.frame.height - visible.height, 0))
            NSAnimationContext.runAnimationGroup { _ in
                scrollView.contentView.animator().setBoundsOrigin(NSPoint(x: 0, y: top))
            }
            scrollView.reflectScrolledClipView(scrollView.contentView)
        }
    }
}

private final class TranscriptScrollView: NSScrollView {
    var userScrolled: () -> Void = {}

    override func scrollWheel(with event: NSEvent) {
        userScrolled()
        super.scrollWheel(with: event)
    }
}

private final class SentenceTextView: NSTextView {
    weak var coordinator: TranscriptTextView.Coordinator?

    override func keyDown(with event: NSEvent) {
        let modifiers = event.modifierFlags.intersection(.deviceIndependentFlagsMask).subtracting([.function, .numericPad])
        guard modifiers.isEmpty, let player = coordinator?.parent?.player else { return super.keyDown(with: event) }
        switch event.specialKey {
        case .leftArrow: player.skip(by: -5)
        case .rightArrow: player.skip(by: 5)
        default:
            guard event.charactersIgnoringModifiers == " " else { return super.keyDown(with: event) }
            player.toggle()
        }
    }

    override func mouseDown(with event: NSEvent) {
        super.mouseDown(with: event)
        guard event.clickCount == 1, selectedRange().length == 0,
              let index = coordinator?.sentence(at: convert(event.locationInWindow, from: nil))
        else { return }
        coordinator?.parent?.play(index)
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        guard let coordinator, let index = coordinator.sentence(at: convert(event.locationInWindow, from: nil)) else {
            return super.menu(for: event)
        }
        return coordinator.menu(for: index, copyingSelection: selectedRange().length > 0)
    }
}

private final class ActionItem: NSMenuItem {
    private let handler: () -> Void

    init(_ title: String, checked: Bool = false, handler: @escaping () -> Void) {
        self.handler = handler
        super.init(title: title, action: #selector(run), keyEquivalent: "")
        target = self
        state = checked ? .on : .off
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    @objc private func run() {
        handler()
    }
}

private extension NSAttributedString.Key {
    static let sentence = NSAttributedString.Key("SillokSentence")
}

@MainActor
private enum TranscriptText {
    static func make(_ document: TranscriptDocument) -> (NSAttributedString, [NSRange]) {
        let transcript = document.transcript
        let speakers = transcript.segments.contains { $0.speaker != nil }
        let text = NSMutableAttributedString()
        var sentences: [NSRange] = []
        for (index, segment) in transcript.segments.enumerated() {
            let previous = index > 0 ? transcript.segments[index - 1] : nil
            if let previous, !startsParagraph(segment, after: previous, speakers: speakers) {
                text.append(NSAttributedString(string: separator(after: previous.text), attributes: body))
            } else {
                text.append(header(for: segment, index: index, in: transcript, first: previous == nil))
            }
            let start = text.length
            var attributes = body
            attributes[.sentence] = index
            if document.edits.isEdited(index) {
                attributes[.underlineStyle] = NSUnderlineStyle.single.union(.patternDot).rawValue
                attributes[.underlineColor] = NSColor.secondaryLabelColor
                attributes[.toolTip] = String(localized: "Edited. The original: \(document.originalText(of: index))")
            }
            text.append(NSAttributedString(string: segment.text, attributes: attributes))
            sentences.append(NSRange(location: start, length: text.length - start))
        }
        return (text, sentences)
    }

    private static func startsParagraph(_ segment: Transcript.Segment, after previous: Transcript.Segment,
                                        speakers: Bool) -> Bool {
        speakers ? segment.speaker != previous.speaker
            : segment.startMs - previous.endMs >= TranscriptTextView.paragraphPause
    }

    private static func separator(after text: String) -> String {
        text.last.map { "。！？".contains($0) } == true ? "" : " "
    }

    private static func header(for segment: Transcript.Segment, index: Int, in transcript: Transcript,
                               first: Bool) -> NSAttributedString {
        let style = NSMutableParagraphStyle()
        style.paragraphSpacingBefore = first ? 0 : 14
        style.paragraphSpacing = 2
        let header = NSMutableAttributedString(string: first ? "" : "\n")
        if let speaker = segment.speaker {
            header.append(NSAttributedString(string: Speakers.name(speaker, in: transcript) + "  ", attributes: [
                .font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize, weight: .semibold),
                .foregroundColor: NSColor.controlAccentColor,
            ]))
        }
        header.append(NSAttributedString(string: Timestamp.text(segment.startMs) + "\n", attributes: [
            .font: NSFont.monospacedDigitSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular),
            .foregroundColor: NSColor.secondaryLabelColor,
        ]))
        header.addAttributes([.paragraphStyle: style, .sentence: index], range: NSRange(location: 0, length: header.length))
        return header
    }

    private static let body: [NSAttributedString.Key: Any] = {
        let style = NSMutableParagraphStyle()
        style.lineSpacing = 4
        style.lineBreakStrategy = [.pushOut, .hangulWordPriority]
        return [.font: NSFont.systemFont(ofSize: 14), .foregroundColor: NSColor.labelColor, .paragraphStyle: style]
    }()
}
