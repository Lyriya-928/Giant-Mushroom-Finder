package dev.sakuhime.mushroomfinder;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextField;
import javax.swing.UIManager;
import javax.swing.border.Border;
import javax.swing.table.JTableHeader;
import javax.swing.table.TableColumn;
import java.awt.AlphaComposite;
import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Component;
import java.awt.Cursor;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.GraphicsEnvironment;
import java.awt.Insets;
import java.awt.LayoutManager;
import java.awt.RenderingHints;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import javax.swing.Scrollable;
import javax.swing.Timer;
import java.awt.Rectangle;

/**
 * Lightweight Fluent-inspired theme for Swing.
 *
 * The application intentionally keeps the standard Swing architecture: this
 * class only centralises colours, typography, rounded surfaces and a few
 * custom-painted controls. No third-party UI dependency is added.
 */
public final class FluentTheme {

    public static final Color BG = new Color(0xF3F4F6);
    public static final Color CARD = Color.WHITE;
    public static final Color CARD_SUBTLE = new Color(0xF8F9FB);
    public static final Color BORDER = new Color(0xE1E4E8);
    public static final Color BORDER_STRONG = new Color(0xC9CED6);
    public static final Color TEXT = new Color(0x1B1D21);
    public static final Color MUTED = new Color(0x636870);
    public static final Color ACCENT = new Color(0x0F6CBD);
    public static final Color ACCENT_HOVER = new Color(0x115EA3);
    public static final Color ACCENT_PRESSED = new Color(0x0C3B5E);
    public static final Color ACCENT_SOFT = new Color(0xE5F1FB);
    public static final Color WARN = new Color(0xA15C00);
    public static final Color WARN_SOFT = new Color(0xFFF4CE);
    public static final Color OK = new Color(0x0F7B0F);
    public static final Color OK_SOFT = new Color(0xE7F6E7);
    public static final Color TABLE_HEADER = new Color(0xFAFBFC);
    public static final Color ROW_ALT = new Color(0xFBFCFD);

    public static final int RADIUS = 10;

    private static final String FAMILY = pickFamily();

    private FluentTheme() {}

    private static String pickFamily() {
        try {
            Set<String> available = new HashSet<>(
                    Arrays.asList(GraphicsEnvironment.getLocalGraphicsEnvironment()
                            .getAvailableFontFamilyNames()));
            for (String candidate : new String[] {
                    "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI Variable Text",
                    "Segoe UI", "Dialog" }) {
                if (available.contains(candidate)) return candidate;
            }
        } catch (Throwable ignored) {
            // headless / restricted environment
        }
        return "Dialog";
    }

    public static Font font(int style, int size) {
        return new Font(FAMILY, style, size);
    }

    public static Font titleFont() { return font(Font.BOLD, 22); }
    public static Font subtitleFont() { return font(Font.PLAIN, 12); }
    public static Font sectionFont() { return font(Font.BOLD, 13); }
    public static Font bodyFont() { return font(Font.PLAIN, 13); }
    public static Font smallFont() { return font(Font.PLAIN, 11); }
    public static Font monoFont(int size) {
        return new Font("Consolas", Font.PLAIN, size);
    }

    public static void applyGlobalDefaults() {
        UIManager.put("ToolTip.background", CARD);
        UIManager.put("ToolTip.foreground", TEXT);
        UIManager.put("ToolTip.border", BorderFactory.createCompoundBorder(
                new RoundedBorder(BORDER_STRONG, 6),
                BorderFactory.createEmptyBorder(6, 8, 6, 8)));
        UIManager.put("ToolTip.font", smallFont());
        UIManager.put("ScrollBar.width", 10);
        UIManager.put("ScrollBar.thumb", new Color(0xC7CBD1));
        UIManager.put("ScrollBar.track", new Color(0x00000000));
        UIManager.put("OptionPane.messageFont", bodyFont());
        UIManager.put("OptionPane.buttonFont", bodyFont());
        UIManager.put("FileChooser.font", bodyFont());
    }

    // ---- containers ----

    public static CardPanel card(LayoutManager layout) {
        CardPanel p = new CardPanel();
        if (layout != null) p.setLayout(layout);
        return p;
    }

    public static JPanel transparent(LayoutManager layout) {
        JPanel p = new JPanel(layout);
        p.setOpaque(false);
        return p;
    }

    public static JLabel title(String text) {
        JLabel l = new JLabel(text);
        l.setFont(titleFont());
        l.setForeground(TEXT);
        return l;
    }

    public static JLabel subtitle(String text) {
        JLabel l = new JLabel(text);
        l.setFont(subtitleFont());
        l.setForeground(MUTED);
        return l;
    }

    public static JLabel section(String text) {
        JLabel l = new JLabel(text);
        l.setFont(sectionFont());
        l.setForeground(TEXT);
        return l;
    }

    public static JLabel body(String text) {
        JLabel l = new JLabel(text);
        l.setFont(bodyFont());
        l.setForeground(TEXT);
        return l;
    }

    public static JLabel hint(String text) {
        JLabel l = new JLabel(text);
        l.setFont(smallFont());
        l.setForeground(MUTED);
        return l;
    }

    public static JLabel muted(String text) {
        JLabel l = new JLabel(text);
        l.setFont(bodyFont());
        l.setForeground(MUTED);
        return l;
    }

    public static JComponent hsep() {
        JPanel p = new JPanel();
        p.setOpaque(false);
        p.setPreferredSize(new Dimension(1, 1));
        p.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, BORDER));
        return p;
    }

    // ---- fields ----

    public static void styleField(JTextField f) {
        f.setFont(bodyFont());
        f.setForeground(TEXT);
        f.setBackground(CARD);
        f.setCaretColor(TEXT);
        f.setBorder(new RoundedBorder(BORDER_STRONG, 7));
        f.setPreferredSize(new Dimension(f.getPreferredSize().width, 30));
    }

    public static void styleCombo(JComboBox<?> box) {
        box.setFont(bodyFont());
        box.setForeground(TEXT);
        box.setBackground(CARD);
        box.setBorder(new RoundedBorder(BORDER_STRONG, 7));
        box.setPreferredSize(new Dimension(box.getPreferredSize().width, 30));
    }

    public static JButton accentButton(String text) {
        return new AccentButton(text);
    }

    public static JButton ghostButton(String text) {
        return new GhostButton(text);
    }

    public static JScrollPane scroll(Component view) {
        JScrollPane sp = new JScrollPane(view);
        sp.setBorder(BorderFactory.createEmptyBorder());
        sp.getViewport().setBackground(CARD);
        sp.setOpaque(false);
        sp.getVerticalScrollBar().setUnitIncrement(18);
        return sp;
    }

    public static void styleTable(JTable table) {
        table.setFont(bodyFont());
        table.setForeground(TEXT);
        table.setBackground(CARD);
        table.setRowHeight(30);
        table.setShowGrid(false);
        table.setIntercellSpacing(new Dimension(0, 0));
        table.setFillsViewportHeight(true);
        table.setSelectionBackground(ACCENT_SOFT);
        table.setSelectionForeground(TEXT);
        table.setAutoCreateRowSorter(true);
        JTableHeader header = table.getTableHeader();
        header.setFont(font(Font.BOLD, 12));
        header.setForeground(MUTED);
        header.setBackground(TABLE_HEADER);
        header.setReorderingAllowed(false);
        header.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, BORDER));
        header.setPreferredSize(new Dimension(header.getPreferredSize().width, 32));
    }

    public static void alignColumn(JTable table, int index, int width) {
        TableColumn c = table.getColumnModel().getColumn(index);
        c.setPreferredWidth(width);
    }

    // ---- custom components ----

    public static final class CardPanel extends JPanel {
        public CardPanel() {
            setOpaque(false);
            setBorder(BorderFactory.createEmptyBorder(12, 16, 12, 16));
        }

        @Override
        public Dimension getMaximumSize() {
            return new Dimension(Integer.MAX_VALUE, getPreferredSize().height);
        }

        @Override
        protected void paintComponent(Graphics g) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                int w = getWidth();
                int h = getHeight();
                // soft shadow
                g2.setComposite(AlphaComposite.SrcOver.derive(0.10f));
                g2.setColor(Color.BLACK);
                g2.fillRoundRect(1, 3, w - 2, h - 4, RADIUS + 2, RADIUS + 2);
                g2.setComposite(AlphaComposite.SrcOver);
                g2.setColor(CARD);
                g2.fillRoundRect(0, 0, w - 2, h - 4, RADIUS + 2, RADIUS + 2);
                g2.setColor(BORDER);
                g2.setStroke(new BasicStroke(1f));
                g2.drawRoundRect(0, 0, w - 3, h - 5, RADIUS + 2, RADIUS + 2);
            } finally {
                g2.dispose();
            }
            super.paintComponent(g);
        }
    }

    public static final class RoundedBorder implements Border {
        private final Color color;
        private final int radius;

        public RoundedBorder(Color color, int radius) {
            this.color = color;
            this.radius = radius;
        }

        @Override
        public Insets getBorderInsets(Component c) {
            return new Insets(5, 9, 5, 9);
        }

        @Override
        public boolean isBorderOpaque() { return false; }

        @Override
        public void paintBorder(Component c, Graphics g, int x, int y, int w, int h) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                g2.setColor(color);
                g2.drawRoundRect(x, y, w - 1, h - 1, radius, radius);
            } finally {
                g2.dispose();
            }
        }
    }

    private abstract static class BaseButton extends JButton {
        BaseButton(String text) {
            super(text);
            setFont(font(Font.BOLD, 12));
            setFocusPainted(false);
            setBorderPainted(false);
            setContentAreaFilled(false);
            setOpaque(false);
            setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
            setMargin(new Insets(7, 14, 7, 14));
            setPreferredSize(new Dimension(getPreferredSize().width, 34));
        }

        @Override
        public void setEnabled(boolean enabled) {
            super.setEnabled(enabled);
            setCursor(enabled ? Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
                    : Cursor.getDefaultCursor());
            repaint();
        }
    }

    public static final class AccentButton extends BaseButton {
        AccentButton(String text) {
            super(text);
            setForeground(Color.WHITE);
            setFont(font(Font.BOLD, 13));
        }

        @Override
        protected void paintComponent(Graphics g) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                Color fill;
                if (!isEnabled()) fill = new Color(0xC9D3DD);
                else if (getModel().isPressed()) fill = ACCENT_PRESSED;
                else if (getModel().isRollover()) fill = ACCENT_HOVER;
                else fill = ACCENT;
                g2.setColor(fill);
                g2.fillRoundRect(0, 0, getWidth(), getHeight(), 8, 8);
            } finally {
                g2.dispose();
            }
            super.paintComponent(g);
        }
    }

    public static final class GhostButton extends BaseButton {
        GhostButton(String text) {
            super(text);
            setForeground(TEXT);
        }

        @Override
        protected void paintComponent(Graphics g) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                Color fill = getModel().isRollover() ? new Color(0xF3F5F8) : CARD;
                if (getModel().isPressed()) fill = new Color(0xE9EDF2);
                if (!isEnabled()) fill = CARD_SUBTLE;
                g2.setColor(fill);
                g2.fillRoundRect(0, 0, getWidth() - 1, getHeight() - 1, 8, 8);
                g2.setColor(isEnabled() ? BORDER_STRONG : BORDER);
                g2.drawRoundRect(0, 0, getWidth() - 1, getHeight() - 1, 8, 8);
            } finally {
                g2.dispose();
            }
            super.paintComponent(g);
        }
    }

    /** Rounded determinate / indeterminate progress bar. */
    public static final class ProgressBar extends JComponent {
        private int value = 0;
        private boolean indeterminate = false;
        private float phase = 0f;
        private final Timer timer;
        private String caption = "";

        public ProgressBar() {
            setOpaque(false);
            setPreferredSize(new Dimension(200, 30));
            timer = new Timer(33, e -> {
                if (!indeterminate) return;
                phase += 0.035f;
                if (phase > 1f) phase -= 1f;
                repaint();
            });
        }

        public void setValue(int v) {
            value = Math.max(0, Math.min(100, v));
            repaint();
        }

        public int value() { return value; }

        public void setIndeterminate(boolean ind) {
            if (indeterminate == ind) return;
            indeterminate = ind;
            if (ind) timer.start(); else timer.stop();
            repaint();
        }

        public void setCaption(String caption) {
            this.caption = caption == null ? "" : caption;
            repaint();
        }

        @Override
        protected void paintComponent(Graphics g) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                int textW = 0;
                FontMetrics fm = g2.getFontMetrics(smallFont());
                if (!caption.isEmpty()) {
                    g2.setFont(smallFont());
                    textW = fm.stringWidth(caption) + 10;
                }
                int barW = Math.max(20, getWidth() - textW);
                int barH = 8;
                int y = (getHeight() - barH) / 2;
                g2.setColor(new Color(0xE7EAEE));
                g2.fillRoundRect(0, y, barW, barH, barH, barH);
                g2.setColor(ACCENT);
                if (indeterminate) {
                    int seg = Math.max(24, barW / 3);
                    int x = (int) ((barW + seg) * phase) - seg;
                    g2.fillRoundRect(Math.max(0, x), y, seg, barH, barH, barH);
                } else {
                    int fill = (int) Math.round(barW * (value / 100.0));
                    if (fill > 0) g2.fillRoundRect(0, y, Math.max(barH, fill), barH, barH, barH);
                }
                if (!caption.isEmpty()) {
                    g2.setColor(MUTED);
                    g2.setFont(smallFont());
                    g2.drawString(caption, barW + 10,
                            (getHeight() + fm.getAscent() - fm.getDescent()) / 2);
                }
            } finally {
                g2.dispose();
            }
        }
    }

    /** Vertical column that tracks the scroll viewport width instead of its content width. */
    public static final class ScrollableColumn extends JPanel implements Scrollable {
        public ScrollableColumn() {
            setOpaque(false);
        }

        @Override public Dimension getPreferredScrollableViewportSize() {
            return getPreferredSize();
        }

        @Override public int getScrollableUnitIncrement(Rectangle visibleRect,
                                                         int orientation, int direction) {
            return 18;
        }

        @Override public int getScrollableBlockIncrement(Rectangle visibleRect,
                                                          int orientation, int direction) {
            return Math.max(40, visibleRect.height - 24);
        }

        @Override public boolean getScrollableTracksViewportWidth() { return true; }

        @Override public boolean getScrollableTracksViewportHeight() { return false; }
    }

    /** Small rounded status pill. */
    public static final class PillLabel extends JLabel {
        private Color fill = ACCENT_SOFT;
        private Color line = BORDER;

        public PillLabel(String text) {
            super(text);
            setFont(smallFont());
            setForeground(ACCENT);
            setBorder(BorderFactory.createEmptyBorder(3, 8, 3, 8));
        }

        public void setTone(Color fill, Color line, Color fg) {
            this.fill = fill;
            this.line = line;
            setForeground(fg);
            repaint();
        }

        @Override
        protected void paintComponent(Graphics g) {
            Graphics2D g2 = (Graphics2D) g.create();
            try {
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                        RenderingHints.VALUE_ANTIALIAS_ON);
                g2.setColor(fill);
                g2.fillRoundRect(0, 0, getWidth() - 1, getHeight() - 1, 12, 12);
                g2.setColor(line);
                g2.drawRoundRect(0, 0, getWidth() - 1, getHeight() - 1, 12, 12);
            } finally {
                g2.dispose();
            }
            super.paintComponent(g);
        }
    }

    /** Adds hover feedback to a card that behaves like a button (history rows). */
    public static void hoverHighlight(JComponent c, Color normal, Color hover) {
        c.setBackground(normal);
        c.addMouseListener(new MouseAdapter() {
            @Override public void mouseEntered(MouseEvent e) { c.setBackground(hover); }
            @Override public void mouseExited(MouseEvent e) { c.setBackground(normal); }
        });
    }
}
