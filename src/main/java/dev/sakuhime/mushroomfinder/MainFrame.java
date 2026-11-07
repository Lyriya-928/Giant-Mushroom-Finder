package dev.sakuhime.mushroomfinder;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSpinner;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.ListSelectionModel;
import javax.swing.SpinnerNumberModel;
import javax.swing.SwingConstants;
import javax.swing.SwingUtilities;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import javax.swing.table.AbstractTableModel;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.TableRowSorter;
import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Cursor;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.Toolkit;
import java.awt.datatransfer.StringSelection;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import javax.swing.RowSorter;
import javax.swing.SortOrder;

/**
 * Main Swing window.
 *
 * Search lifecycle pattern inspired by SunnySlopes FortressFinderGUI.
 * No source code copied. The visual layer is a lightweight Fluent-inspired
 * theme ({@link FluentTheme}); the application remains a plain Swing app.
 */
public final class MainFrame extends JFrame implements SearchRunner.Listener {

    private static final int COL_RANK = 0;
    private static final int COL_AREA = 3;
    private static final int COL_KM2 = 4;
    private static final int COL_DIST = 6;
    private static final int COL_COMPACT = 7;
    private static final int COL_PRECISION = 8;

    private final UserPrefs prefs = UserPrefs.load();

    // Search form
    private final JComboBox<String> seedBox = new JComboBox<>();
    private final JComboBox<String> versionBox = new JComboBox<>();
    private final JTextField centerXField = new JTextField(7);
    private final JTextField centerZField = new JTextField(7);
    private final JComboBox<String> radiusBox = new JComboBox<>();
    private final JComboBox<String> targetBox = new JComboBox<>();
    private final List<BiomePreset> targetPresets = BiomePreset.all();
    private final JTextField customBiomesField = new JTextField(16);
    private final JLabel customBiomesLabel = new JLabel();
    private final JComboBox<String> recentBox = new JComboBox<>();
    private final JButton clearRecentBtn;

    // Options
    private final JSpinner minAreaSpinner =
            new JSpinner(new SpinnerNumberModel(0L, 0L, 1_000_000_000L, 1000L));
    private final JSpinner maxResultsSpinner =
            new JSpinner(new SpinnerNumberModel(10, 0, 500, 1));
    private final JComboBox<String> threadsBox = new JComboBox<>();
    private final JComboBox<String> scanModeBox = new JComboBox<>();
    private final JComboBox<String> tileSizeBox = new JComboBox<>();
    private final JCheckBox refineCheck = new JCheckBox();
    private final JComboBox<String> langBox = new JComboBox<>();
    private final JComboBox<String> copyFormatBox = new JComboBox<>();

    private final JButton searchBtn;
    private final JButton pauseBtn;
    private final JButton stopBtn;
    private final JButton copyBtn;
    private final JButton exportCsvBtn;
    private final JButton exportJsonBtn;

    // Headers / labels
    private final JLabel appTitleLabel = new JLabel();
    private final JLabel appSubtitleLabel = new JLabel();
    private final JLabel seedLabel = new JLabel();
    private final JLabel searchSectionLabel = new JLabel();
    private final JLabel optionsSectionLabel = new JLabel();
    private final JLabel versionLabel = new JLabel();
    private final JLabel centerLabel = new JLabel();
    private final JLabel radiusLabel = new JLabel();
    private final JLabel targetLabel = new JLabel();
    private final JLabel recentLabel = new JLabel();
    private final JLabel minAreaLabel = new JLabel();
    private final JLabel maxResultsLabel = new JLabel();
    private final JLabel threadsLabel = new JLabel();
    private final JLabel scanModeLabel = new JLabel();
    private final JLabel tileSizeLabel = new JLabel();
    private final JLabel langLabel = new JLabel();
    private final JLabel copyFormatLabel = new JLabel();
    private final JLabel planLabel = new JLabel();
    private final JLabel statusLabel = new JLabel();
    private final JLabel timeLabel = new JLabel();
    private final JLabel resultsTitleLabel = new JLabel();
    private final JLabel resultsSubtitleLabel = new JLabel();
    private final FluentTheme.PillLabel countPill =
            new FluentTheme.PillLabel("");
    private final FluentTheme.PillLabel modePill =
            new FluentTheme.PillLabel("");
    private final JLabel emptyLabel = new JLabel();
    private final FluentTheme.ProgressBar progressBar = new FluentTheme.ProgressBar();

    private final JPanel sidebar = new FluentTheme.ScrollableColumn();
    private final JPanel emptyPanel = new JPanel(new BorderLayout());
    private JScrollPane tableScroll;
    private JPanel resultsCenter;
    private FluentTheme.CardPanel resultsCard;
    private final ResultTableModel tableModel = new ResultTableModel();
    private final JTable resultTable = new JTable(tableModel);

    private final SearchRunner runner = new SearchRunner();
    private List<SearchResult> lastResults = List.of();
    private SearchSettings lastSettings;
    private NativeBridge.SearchPlan lastPlan;
    private List<String> recentEntries = new ArrayList<>();

    private boolean searching = false;
    private boolean updatingFields = false;
    private long searchStartNanos = 0L;
    private long pauseStartNanos = 0L;
    private long pausedTotalNanos = 0L;
    private int lastPercent = 0;
    private javax.swing.Timer planTimer;

    public MainFrame() {
        super(I18n.appTitle());

        if ("zh".equalsIgnoreCase(prefs.language)) I18n.set(I18n.Lang.ZH);
        else if ("en".equalsIgnoreCase(prefs.language)) I18n.set(I18n.Lang.EN);

        clearRecentBtn = FluentTheme.ghostButton(I18n.clearRecent());
        searchBtn = FluentTheme.accentButton(I18n.search());
        pauseBtn = FluentTheme.ghostButton(I18n.pause());
        stopBtn = FluentTheme.ghostButton(I18n.stop());
        copyBtn = FluentTheme.ghostButton(I18n.copyCoords());
        exportCsvBtn = FluentTheme.ghostButton(I18n.exportCsv());
        exportJsonBtn = FluentTheme.ghostButton(I18n.exportJson());

        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setMinimumSize(new Dimension(1080, 720));
        setSize(1200, 820);

        loadVersionItems();
        loadRadiusItems();
        loadThreadsItems();
        loadStaticCombos();
        buildUi();
        loadPrefsIntoUi();
        wireEvents();
        applyLanguage();
        runner.addListener(this);

        if (!NativeBridge.isLoaded()) {
            statusLabel.setText(I18n.nativeMissing(NativeBridge.getLoadError()));
            searchBtn.setEnabled(false);
        }

        addWindowListener(new WindowAdapter() {
            @Override public void windowClosing(WindowEvent e) {
                savePrefsFromUi();
            }
        });

        setLocationRelativeTo(null);
        refreshPlan();
    }

    // ------------------------------------------------------------------
    // data loading helpers
    // ------------------------------------------------------------------

    private void loadVersionItems() {
        versionBox.removeAllItems();
        String[] versions = NativeBridge.isLoaded()
                ? NativeBridge.nativeKnownVersions()
                : new String[] {"1.21.3", "1.20", "1.18", "1.16.5"};
        for (String v : versions) versionBox.addItem(v);
        versionBox.setSelectedItem("1.21.3");
    }

    private void loadRadiusItems() {
        Object prev = radiusBox.isEditable()
                ? radiusBox.getEditor().getItem() : radiusBox.getSelectedItem();
        radiusBox.setEditable(true);
        radiusBox.removeAllItems();
        radiusBox.addItem(I18n.radiusHint500());
        radiusBox.addItem(I18n.radiusHint1000());
        radiusBox.addItem(I18n.radiusHint2000());
        radiusBox.addItem(I18n.radiusHint5000());
        if (prev != null) radiusBox.setSelectedItem(prev);
        else radiusBox.setSelectedItem("1000");
        styleEditableCombo(radiusBox);
    }

    private void loadThreadsItems() {
        String sel = selectedText(threadsBox);
        threadsBox.setEditable(true);
        threadsBox.removeAllItems();
        int auto = NativeBridge.isLoaded() ? NativeBridge.defaultThreads()
                : Math.max(1, Runtime.getRuntime().availableProcessors());
        threadsBox.addItem(I18n.autoWith(auto));
        for (int n : new int[] {1, 2, 4, 6, 8, 10, 12, 16, 24, 32}) {
            threadsBox.addItem(Integer.toString(n));
        }
        if (sel != null && !sel.isBlank()) {
            boolean found = false;
            for (int i = 0; i < threadsBox.getItemCount(); i++) {
                if (sel.equals(threadsBox.getItemAt(i))) { found = true; break; }
            }
            if (found) threadsBox.setSelectedItem(sel);
            else threadsBox.setSelectedIndex(0);
        } else {
            threadsBox.setSelectedIndex(0);
        }
        styleEditableCombo(threadsBox);
    }

    private void loadStaticCombos() {
        langBox.removeAllItems();
        for (I18n.Lang l : I18n.Lang.values()) langBox.addItem(I18n.langDisplayName(l));
        langBox.setSelectedItem(I18n.langDisplayName(I18n.get()));

        targetBox.removeAllItems();
        for (BiomePreset preset : targetPresets) targetBox.addItem(preset.label());

        scanModeBox.removeAllItems();
        scanModeBox.addItem(I18n.scanAuto());
        scanModeBox.addItem(I18n.scanFull());
        scanModeBox.addItem(I18n.scanTiled());

        tileSizeBox.removeAllItems();
        tileSizeBox.addItem(I18n.tileAuto());
        for (String s : new String[] {"128", "256", "512", "1024", "2048"}) {
            tileSizeBox.addItem(s);
        }

        for (String f : I18n.copyFormats()) copyFormatBox.addItem(f);
    }

    private BiomePreset selectedPreset() {
        int idx = targetBox.getSelectedIndex();
        if (idx < 0 || idx >= targetPresets.size()) return targetPresets.get(0);
        return targetPresets.get(idx);
    }

    private void rebuildTargetBox(int selectedIndex) {
        targetBox.removeAllItems();
        for (BiomePreset preset : targetPresets) targetBox.addItem(preset.label());
        int idx = Math.max(0, Math.min(targetPresets.size() - 1, selectedIndex));
        targetBox.setSelectedIndex(idx);
    }

    private static int maxRadiusScale16() {
        try {
            return NativeBridge.maxRadius(16);
        } catch (Throwable t) {
            return 0;
        }
    }

    private void updateRadiusLimitLabel() {
        int max = maxRadiusScale16();
        if (max > 0) {
            radiusLabel.setText(I18n.radiusWithMax(max));
            radiusBox.setToolTipText(I18n.tipRadiusMax(max));
        } else {
            radiusLabel.setText(I18n.radius());
            radiusBox.setToolTipText(I18n.tipRadius());
        }
    }

    private static void styleSpinner(JSpinner spinner) {        spinner.setFont(FluentTheme.bodyFont());
        spinner.setPreferredSize(new Dimension(108, 30));
        spinner.setMinimumSize(new Dimension(80, 30));
        if (spinner.getEditor() instanceof JSpinner.DefaultEditor de) {
            de.getTextField().setFont(FluentTheme.bodyFont());
            de.getTextField().setHorizontalAlignment(SwingConstants.RIGHT);
            de.getTextField().setBorder(BorderFactory.createEmptyBorder(2, 6, 2, 2));
        }
    }

    private static void styleEditableCombo(JComboBox<String> box) {
        FluentTheme.styleCombo(box);
        if (box.isEditable() && box.getEditor() != null
                && box.getEditor().getEditorComponent() instanceof JTextField tf) {
            tf.setBorder(BorderFactory.createEmptyBorder(0, 4, 0, 4));
            tf.setFont(FluentTheme.bodyFont());
            tf.setBackground(FluentTheme.CARD);
            tf.setForeground(FluentTheme.TEXT);
        }
    }

    private static String selectedText(JComboBox<String> box) {
        if (box == null) return null;
        Object v = box.isEditable() && box.getEditor() != null
                ? box.getEditor().getItem() : box.getSelectedItem();
        return v == null ? null : String.valueOf(v).trim();
    }

    // ------------------------------------------------------------------
    // UI construction
    // ------------------------------------------------------------------

    private void buildUi() {
        JPanel root = new JPanel(new BorderLayout(0, 14));
        root.setBackground(FluentTheme.BG);
        root.setBorder(BorderFactory.createEmptyBorder(18, 20, 16, 20));

        root.add(buildHeader(), BorderLayout.NORTH);

        sidebar.setLayout(new GridBagLayout());
        sidebar.setOpaque(false);
        GridBagConstraints sg = new GridBagConstraints();
        sg.gridx = 0;
        sg.weightx = 1;
        sg.fill = GridBagConstraints.HORIZONTAL;
        sg.anchor = GridBagConstraints.NORTH;
        sg.gridy = 0;
        sg.insets = new Insets(0, 0, 12, 0);
        sidebar.add(buildSearchCard(), sg);
        sg.gridy = 1;
        sg.insets = new Insets(0, 0, 0, 0);
        sidebar.add(buildOptionsCard(), sg);
        sg.gridy = 2;
        sg.weighty = 1;
        sg.fill = GridBagConstraints.BOTH;
        sidebar.add(Box.createGlue(), sg);

        JScrollPane sideScroll = new JScrollPane(sidebar);
        sideScroll.setBorder(BorderFactory.createEmptyBorder());
        sideScroll.setOpaque(false);
        sideScroll.getViewport().setOpaque(false);
        sideScroll.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        sideScroll.getVerticalScrollBar().setUnitIncrement(18);
        sideScroll.setPreferredSize(new Dimension(420, 100));

        JPanel body = new JPanel(new BorderLayout(16, 0));
        body.setOpaque(false);
        body.add(sideScroll, BorderLayout.WEST);

        resultsCard = buildResultsCard();
        body.add(resultsCard, BorderLayout.CENTER);
        root.add(body, BorderLayout.CENTER);

        setContentPane(root);
    }

    private JPanel buildHeader() {
        JPanel header = new JPanel(new BorderLayout(16, 0));
        header.setOpaque(false);

        JPanel titles = new JPanel();
        titles.setOpaque(false);
        titles.setLayout(new BoxLayout(titles, BoxLayout.Y_AXIS));
        appTitleLabel.setText(I18n.appTitle());
        appTitleLabel.setFont(FluentTheme.titleFont());
        appTitleLabel.setForeground(FluentTheme.TEXT);
        appTitleLabel.setAlignmentX(Component.LEFT_ALIGNMENT);
        appTitleLabel.setBorder(BorderFactory.createEmptyBorder(0, 0, 0, 24));
        appSubtitleLabel.setText(I18n.appSubtitle());
        appSubtitleLabel.setFont(FluentTheme.subtitleFont());
        appSubtitleLabel.setForeground(FluentTheme.MUTED);
        appSubtitleLabel.setAlignmentX(Component.LEFT_ALIGNMENT);
        appSubtitleLabel.setBorder(BorderFactory.createEmptyBorder(0, 0, 0, 24));
        titles.add(appTitleLabel);
        titles.add(Box.createVerticalStrut(3));
        titles.add(appSubtitleLabel);
        header.add(titles, BorderLayout.CENTER);

        JPanel right = new JPanel(new FlowLayout(FlowLayout.RIGHT, 8, 6));
        right.setOpaque(false);
        langLabel.setText(I18n.language());
        langLabel.setFont(FluentTheme.smallFont());
        langLabel.setForeground(FluentTheme.MUTED);
        FluentTheme.styleCombo(langBox);
        right.add(langLabel);
        right.add(langBox);
        header.add(right, BorderLayout.EAST);
        return header;
    }

    private FluentTheme.CardPanel buildSearchCard() {
        FluentTheme.CardPanel card = FluentTheme.card(new GridBagLayout());
        card.setAlignmentX(Component.LEFT_ALIGNMENT);
        GridBagConstraints gc = baseConstraints();

        int row = 0;
        searchSectionLabel.setText(I18n.basicSettings());
        searchSectionLabel.setFont(FluentTheme.sectionFont());
        searchSectionLabel.setForeground(FluentTheme.TEXT);
        addFull(card, gc, row++, searchSectionLabel);

        seedBox.setEditable(true);
        FluentTheme.styleCombo(seedBox);
        styleEditableCombo(seedBox);
        seedBox.setMinimumSize(new Dimension(90, 30));
        addRow(card, gc, row++, seedLabel, seedBox);

        FluentTheme.styleCombo(versionBox);
        versionBox.setMinimumSize(new Dimension(80, 30));
        addRow(card, gc, row++, versionLabel, versionBox);

        JPanel centerPanel = FluentTheme.transparent(new FlowLayout(FlowLayout.LEFT, 6, 0));
        FluentTheme.styleField(centerXField);
        FluentTheme.styleField(centerZField);
        centerXField.setHorizontalAlignment(SwingConstants.RIGHT);
        centerZField.setHorizontalAlignment(SwingConstants.RIGHT);
        JLabel xTag = FluentTheme.hint("X");
        JLabel zTag = FluentTheme.hint("Z");
        centerPanel.add(xTag);
        centerPanel.add(centerXField);
        centerPanel.add(zTag);
        centerPanel.add(centerZField);
        addRow(card, gc, row++, centerLabel, centerPanel);

        FluentTheme.styleCombo(radiusBox);
        radiusBox.setMinimumSize(new Dimension(70, 30));
        JPanel radiusPanel = FluentTheme.transparent(new BorderLayout(6, 0));
        radiusPanel.add(radiusBox, BorderLayout.CENTER);
        addRow(card, gc, row++, radiusLabel, radiusPanel);

        FluentTheme.styleCombo(targetBox);
        targetBox.setMinimumSize(new Dimension(120, 30));
        addRow(card, gc, row++, targetLabel, targetBox);

        FluentTheme.styleField(customBiomesField);
        customBiomesField.setMinimumSize(new Dimension(100, 30));
        addRow(card, gc, row++, customBiomesLabel, customBiomesField);
        customBiomesLabel.setText(I18n.customBiomeIds());
        customBiomesField.setToolTipText(I18n.customBiomeHint());

        JPanel recentPanel = FluentTheme.transparent(new BorderLayout(6, 0));
        FluentTheme.styleCombo(recentBox);
        recentBox.setMinimumSize(new Dimension(100, 30));
        recentPanel.add(recentBox, BorderLayout.CENTER);
        clearRecentBtn.setPreferredSize(new Dimension(60, 30));
        recentPanel.add(clearRecentBtn, BorderLayout.EAST);
        addRow(card, gc, row++, recentLabel, recentPanel);

        addFull(card, gc, row++, FluentTheme.hsep());

        planLabel.setFont(FluentTheme.smallFont());
        planLabel.setForeground(FluentTheme.MUTED);
        planLabel.setVerticalAlignment(SwingConstants.TOP);
        addFull(card, gc, row++, planLabel);

        return card;
    }

    private FluentTheme.CardPanel buildOptionsCard() {
        FluentTheme.CardPanel card = FluentTheme.card(new GridBagLayout());
        card.setAlignmentX(Component.LEFT_ALIGNMENT);
        GridBagConstraints gc = baseConstraints();

        int row = 0;
        optionsSectionLabel.setText(I18n.options());
        optionsSectionLabel.setFont(FluentTheme.sectionFont());
        optionsSectionLabel.setForeground(FluentTheme.TEXT);
        addFull(card, gc, row++, optionsSectionLabel);

        styleSpinner(minAreaSpinner);
        styleSpinner(maxResultsSpinner);
        addFull(card, gc, row++, twoFields(minAreaLabel, minAreaSpinner,
                maxResultsLabel, maxResultsSpinner));

        FluentTheme.styleCombo(threadsBox);
        FluentTheme.styleCombo(scanModeBox);
        threadsBox.setMinimumSize(new Dimension(80, 30));
        scanModeBox.setMinimumSize(new Dimension(110, 30));
        addFull(card, gc, row++, twoFields(threadsLabel, threadsBox,
                scanModeLabel, scanModeBox));

        FluentTheme.styleCombo(tileSizeBox);
        FluentTheme.styleCombo(copyFormatBox);
        tileSizeBox.setMinimumSize(new Dimension(70, 30));
        copyFormatBox.setMinimumSize(new Dimension(70, 30));
        addFull(card, gc, row++, twoFields(tileSizeLabel, tileSizeBox,
                copyFormatLabel, copyFormatBox));

        refineCheck.setOpaque(false);
        refineCheck.setFont(FluentTheme.bodyFont());
        refineCheck.setForeground(FluentTheme.TEXT);
        addFull(card, gc, row++, refineCheck);

        addFull(card, gc, row++, FluentTheme.hsep());

        addFull(card, gc, row++, searchBtn);

        JPanel controlRow = FluentTheme.transparent(new GridBagLayout());
        GridBagConstraints cc = new GridBagConstraints();
        cc.gridy = 0;
        cc.insets = new Insets(0, 0, 0, 6);
        cc.fill = GridBagConstraints.HORIZONTAL;
        cc.weightx = 1;
        controlRow.add(pauseBtn, cc);
        cc.gridx = 1;
        cc.insets = new Insets(0, 6, 0, 0);
        controlRow.add(stopBtn, cc);
        addFull(card, gc, row++, controlRow);

        return card;
    }

    private JPanel twoFields(JLabel l1, JComponent f1, JLabel l2, JComponent f2) {
        JPanel p = FluentTheme.transparent(new GridBagLayout());
        GridBagConstraints g = new GridBagConstraints();
        g.gridy = 0;
        g.anchor = GridBagConstraints.WEST;
        g.fill = GridBagConstraints.HORIZONTAL;
        l1.setFont(FluentTheme.bodyFont());
        l1.setForeground(FluentTheme.MUTED);
        l2.setFont(FluentTheme.bodyFont());
        l2.setForeground(FluentTheme.MUTED);
        g.gridx = 0;
        g.weightx = 0;
        g.insets = new Insets(4, 0, 4, 8);
        p.add(l1, g);
        g.gridx = 1;
        g.weightx = 0.45;
        g.insets = new Insets(2, 0, 2, 10);
        p.add(f1, g);
        g.gridx = 2;
        g.weightx = 0;
        g.insets = new Insets(4, 0, 4, 8);
        p.add(l2, g);
        g.gridx = 3;
        g.weightx = 0.55;
        g.insets = new Insets(2, 0, 2, 0);
        p.add(f2, g);
        return p;
    }

    private FluentTheme.CardPanel buildResultsCard() {
        FluentTheme.CardPanel card = FluentTheme.card(new BorderLayout(0, 10));

        JPanel north = FluentTheme.transparent(new BorderLayout(8, 0));
        JPanel titleRow = FluentTheme.transparent(new FlowLayout(FlowLayout.LEFT, 8, 0));
        resultsTitleLabel.setText(I18n.results());
        resultsTitleLabel.setFont(FluentTheme.font(Font.BOLD, 15));
        resultsTitleLabel.setForeground(FluentTheme.TEXT);
        titleRow.add(resultsTitleLabel);
        titleRow.add(countPill);
        titleRow.add(modePill);
        north.add(titleRow, BorderLayout.WEST);

        JPanel actions = FluentTheme.transparent(new FlowLayout(FlowLayout.RIGHT, 6, 0));
        copyBtn.setPreferredSize(new Dimension(104, 30));
        exportCsvBtn.setPreferredSize(new Dimension(96, 30));
        exportJsonBtn.setPreferredSize(new Dimension(104, 30));
        actions.add(copyBtn);
        actions.add(exportCsvBtn);
        actions.add(exportJsonBtn);
        north.add(actions, BorderLayout.EAST);

        resultsSubtitleLabel.setText(I18n.tipTable());
        resultsSubtitleLabel.setFont(FluentTheme.smallFont());
        resultsSubtitleLabel.setForeground(FluentTheme.MUTED);
        north.add(resultsSubtitleLabel, BorderLayout.SOUTH);
        card.add(north, BorderLayout.NORTH);

        FluentTheme.styleTable(resultTable);
        resultTable.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        resultTable.setRowSorter(new TableRowSorter<>(tableModel));
        resultTable.setDefaultRenderer(String.class, new DefaultTableCellRenderer() {
            @Override
            public Component getTableCellRendererComponent(JTable t, Object v, boolean sel,
                                                           boolean focus, int row, int col) {
                Component c = super.getTableCellRendererComponent(t, v, sel, focus, row, col);
                setBorder(BorderFactory.createEmptyBorder(0, 10, 0, 10));
                if (!sel) c.setBackground(row % 2 == 0 ? FluentTheme.CARD : FluentTheme.ROW_ALT);
                if (col >= 3 && col != 5) setHorizontalAlignment(SwingConstants.RIGHT);
                else setHorizontalAlignment(SwingConstants.LEFT);
                return c;
            }
        });
        installNumericRenderers();
        resultTable.setToolTipText(I18n.tipTable());
        resultTable.addMouseListener(new MouseAdapter() {
            @Override public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() == 2 && !searching) copySelected();
            }
        });
        resultTable.getSelectionModel().addListSelectionListener(e -> {
            if (!e.getValueIsAdjusting()) updateActionButtons();
        });

        tableScroll = FluentTheme.scroll(resultTable);
        emptyLabel.setText("<html><body style='width:420px;text-align:center'>"
                + I18n.resultsEmpty() + "</body></html>");
        emptyLabel.setHorizontalAlignment(SwingConstants.CENTER);
        emptyLabel.setFont(FluentTheme.bodyFont());
        emptyLabel.setForeground(FluentTheme.MUTED);
        emptyPanel.setOpaque(false);
        emptyPanel.add(emptyLabel, BorderLayout.CENTER);

        resultsCenter = FluentTheme.transparent(new java.awt.CardLayout());
        resultsCenter.add(emptyPanel, "empty");
        resultsCenter.add(tableScroll, "table");
        card.add(resultsCenter, BorderLayout.CENTER);
        ((java.awt.CardLayout) resultsCenter.getLayout()).show(resultsCenter, "empty");

        JPanel progressPanel = FluentTheme.transparent(new BorderLayout(0, 3));
        JPanel progressLine = FluentTheme.transparent(new BorderLayout(12, 0));
        progressLine.add(progressBar, BorderLayout.CENTER);
        timeLabel.setFont(FluentTheme.smallFont());
        timeLabel.setForeground(FluentTheme.MUTED);
        timeLabel.setHorizontalAlignment(SwingConstants.RIGHT);
        progressLine.add(timeLabel, BorderLayout.EAST);
        progressPanel.add(progressLine, BorderLayout.NORTH);
        statusLabel.setFont(FluentTheme.smallFont());
        statusLabel.setForeground(FluentTheme.TEXT);
        progressPanel.add(statusLabel, BorderLayout.SOUTH);
        card.add(progressPanel, BorderLayout.SOUTH);

        countPill.setVisible(false);
        modePill.setVisible(false);
        return card;
    }

    private void installNumericRenderers() {
        DefaultTableCellRenderer longRenderer = new DefaultTableCellRenderer() {
            @Override
            public Component getTableCellRendererComponent(JTable t, Object v, boolean sel,
                                                           boolean focus, int row, int col) {
                Object shown = v instanceof Number n
                        ? String.format(Locale.ROOT, "%,d", n.longValue()) : v;
                Component c = super.getTableCellRendererComponent(t, shown, sel, focus, row, col);
                setBorder(BorderFactory.createEmptyBorder(0, 10, 0, 10));
                setHorizontalAlignment(SwingConstants.RIGHT);
                if (!sel) c.setBackground(row % 2 == 0 ? FluentTheme.CARD : FluentTheme.ROW_ALT);
                return c;
            }
        };
        DefaultTableCellRenderer doubleRenderer = new DefaultTableCellRenderer() {
            @Override
            public Component getTableCellRendererComponent(JTable t, Object v, boolean sel,
                                                           boolean focus, int row, int col) {
                Object shown = v instanceof Number n
                        ? String.format(Locale.ROOT, "%.3f", n.doubleValue()) : v;
                Component c = super.getTableCellRendererComponent(t, shown, sel, focus, row, col);
                setBorder(BorderFactory.createEmptyBorder(0, 10, 0, 10));
                setHorizontalAlignment(SwingConstants.RIGHT);
                if (!sel) c.setBackground(row % 2 == 0 ? FluentTheme.CARD : FluentTheme.ROW_ALT);
                return c;
            }
        };
        resultTable.setDefaultRenderer(Long.class, longRenderer);
        resultTable.setDefaultRenderer(Double.class, doubleRenderer);
        DefaultTableCellRenderer intRenderer = new DefaultTableCellRenderer() {
            @Override
            public Component getTableCellRendererComponent(JTable t, Object v, boolean sel,
                                                           boolean focus, int row, int col) {
                Component c = super.getTableCellRendererComponent(t, v, sel, focus, row, col);
                setBorder(BorderFactory.createEmptyBorder(0, 10, 0, 10));
                setHorizontalAlignment(SwingConstants.RIGHT);
                if (!sel) c.setBackground(row % 2 == 0 ? FluentTheme.CARD : FluentTheme.ROW_ALT);
                return c;
            }
        };
        resultTable.setDefaultRenderer(Integer.class, intRenderer);
        resultTable.setAutoResizeMode(JTable.AUTO_RESIZE_SUBSEQUENT_COLUMNS);
        resultTable.getColumnModel().getColumn(COL_RANK).setMaxWidth(48);
        resultTable.getColumnModel().getColumn(1).setPreferredWidth(66);
        resultTable.getColumnModel().getColumn(2).setPreferredWidth(66);
        resultTable.getColumnModel().getColumn(COL_AREA).setPreferredWidth(88);
        resultTable.getColumnModel().getColumn(COL_KM2).setPreferredWidth(62);
        resultTable.getColumnModel().getColumn(5).setPreferredWidth(92);
        resultTable.getColumnModel().getColumn(COL_DIST).setPreferredWidth(58);
        resultTable.getColumnModel().getColumn(COL_COMPACT).setPreferredWidth(72);
        resultTable.getColumnModel().getColumn(COL_PRECISION).setPreferredWidth(82);
    }

    private static GridBagConstraints baseConstraints() {
        GridBagConstraints gc = new GridBagConstraints();
        gc.gridx = 0;
        gc.gridy = 0;
        gc.insets = new Insets(2, 0, 2, 0);
        gc.anchor = GridBagConstraints.WEST;
        gc.fill = GridBagConstraints.HORIZONTAL;
        return gc;
    }

    private void addRow(JPanel card, GridBagConstraints gc, int row,
                        JLabel label, JComponent field) {
        label.setFont(FluentTheme.bodyFont());
        label.setForeground(FluentTheme.MUTED);
        gc.gridy = row;
        gc.gridx = 0;
        gc.weightx = 0;
        gc.insets = new Insets(3, 0, 3, 10);
        card.add(label, gc);
        gc.gridx = 1;
        gc.weightx = 1;
        gc.insets = new Insets(2, 0, 2, 0);
        card.add(field, gc);
    }

    private void addFull(JPanel card, GridBagConstraints gc, int row, JComponent comp) {
        gc.gridy = row;
        gc.gridx = 0;
        gc.gridwidth = 2;
        gc.weightx = 1;
        gc.insets = new Insets(3, 0, 3, 0);
        card.add(comp, gc);
        gc.gridwidth = 1;
    }

    // ------------------------------------------------------------------
    // events
    // ------------------------------------------------------------------

    private void wireEvents() {
        searchBtn.addActionListener(e -> startSearch());
        pauseBtn.addActionListener(e -> togglePause());
        stopBtn.addActionListener(e -> runner.stop());
        copyBtn.addActionListener(e -> copySelected());
        exportCsvBtn.addActionListener(e -> exportResults(true));
        exportJsonBtn.addActionListener(e -> exportResults(false));
        clearRecentBtn.addActionListener(e -> {
            recentEntries.clear();
            prefs.clearRecent();
            prefs.save();
            rebuildRecentBox();
        });

        recentBox.addActionListener(e -> applyRecentSelection());
        targetBox.addActionListener(e -> {
            updateTargetFieldState();
            refreshPlan();
        });
        scanModeBox.addActionListener(e -> {
            updateScanFieldState();
            refreshPlan();
        });
        tileSizeBox.addActionListener(e -> refreshPlan());
        threadsBox.addActionListener(e -> refreshPlan());
        versionBox.addActionListener(e -> refreshPlan());
        langBox.addActionListener(e -> switchLanguage());

        DocumentListener dl = new DocumentListener() {
            @Override public void insertUpdate(DocumentEvent e) { schedulePlan(); }
            @Override public void removeUpdate(DocumentEvent e) { schedulePlan(); }
            @Override public void changedUpdate(DocumentEvent e) { schedulePlan(); }
        };
        centerXField.getDocument().addDocumentListener(dl);
        centerZField.getDocument().addDocumentListener(dl);
        if (radiusBox.getEditor() != null
                && radiusBox.getEditor().getEditorComponent() instanceof JTextField tf) {
            tf.getDocument().addDocumentListener(dl);
        }
        customBiomesField.getDocument().addDocumentListener(dl);
        minAreaSpinner.addChangeListener(e -> updateActionButtons());
        maxResultsSpinner.addChangeListener(e -> updateActionButtons());
        refineCheck.addActionListener(e -> refreshPlan());
    }

    private void schedulePlan() {
        if (planTimer != null && planTimer.isRunning()) planTimer.stop();
        planTimer = new javax.swing.Timer(220, e -> refreshPlan());
        planTimer.setRepeats(false);
        planTimer.start();
    }

    private void switchLanguage() {
        int idx = langBox.getSelectedIndex();
        I18n.Lang next = (idx == 0) ? I18n.Lang.ZH : I18n.Lang.EN;
        if (next == I18n.get()) return;
        savePrefsFromUi();
        I18n.set(next);
        prefs.language = next == I18n.Lang.ZH ? "zh" : "en";
        prefs.save();
        applyLanguage();
        refreshPlan();
    }

    private void applyLanguage() {
        setTitle(I18n.appTitle());
        appTitleLabel.setText(I18n.appTitle());
        appSubtitleLabel.setText(I18n.appSubtitle());
        searchSectionLabel.setText(I18n.basicSettings());
        optionsSectionLabel.setText(I18n.options());
        seedLabel.setText(I18n.seed());
        versionLabel.setText(I18n.version());
        centerLabel.setText(I18n.center());
        updateRadiusLimitLabel();
        targetLabel.setText(I18n.targetBiome());
        recentLabel.setText(I18n.recent());
        minAreaLabel.setText(I18n.minArea());
        maxResultsLabel.setText(I18n.maxResults());
        threadsLabel.setText(I18n.threads());
        scanModeLabel.setText(I18n.scanMode());
        tileSizeLabel.setText(I18n.tileSize());
        langLabel.setText(I18n.language());
        copyFormatLabel.setText(I18n.copyFormatLabel());
        customBiomesLabel.setText(I18n.customBiomeIds());
        customBiomesField.setToolTipText(I18n.customBiomeHint());

        seedBox.setToolTipText(I18n.tipSeed());
        seedLabel.setToolTipText(I18n.tipSeed());
        versionBox.setToolTipText(I18n.tipVersion());
        centerXField.setToolTipText(I18n.tipCenter());
        centerZField.setToolTipText(I18n.tipCenter());
        radiusBox.setToolTipText(I18n.tipRadius());
        targetBox.setToolTipText(I18n.tipBiome());
        recentBox.setToolTipText(I18n.tipRecent());
        maxResultsSpinner.setToolTipText(I18n.tipMaxResults());
        minAreaSpinner.setToolTipText(I18n.tipMinArea());
        refineCheck.setToolTipText(I18n.tipRefine());
        threadsBox.setToolTipText(I18n.tipThreads());
        scanModeBox.setToolTipText(I18n.tipScanMode());
        tileSizeBox.setToolTipText(I18n.tipTileSize());
        langBox.setToolTipText(I18n.tipLanguage());
        copyFormatBox.setToolTipText(I18n.tipCopyFormat());
        searchBtn.setToolTipText(I18n.tipSearch());
        pauseBtn.setToolTipText(I18n.tipPause());
        stopBtn.setToolTipText(I18n.tipStop());
        copyBtn.setToolTipText(I18n.tipCopy());
        exportCsvBtn.setToolTipText(I18n.tipExportCsv());
        exportJsonBtn.setToolTipText(I18n.tipExportJson());
        resultTable.setToolTipText(I18n.tipTable());

        refineCheck.setText(I18n.refine());
        searchBtn.setText(I18n.search());
        pauseBtn.setText(runner.isPaused() ? I18n.resume() : I18n.pause());
        stopBtn.setText(I18n.stop());
        copyBtn.setText(I18n.copyCoords());
        exportCsvBtn.setText(I18n.exportCsv());
        exportJsonBtn.setText(I18n.exportJson());
        clearRecentBtn.setText(I18n.clearRecent());
        resultsTitleLabel.setText(I18n.results());
        resultsSubtitleLabel.setText(I18n.tipTable());
        emptyLabel.setText("<html><body style='width:420px;text-align:center'>"
                + I18n.resultsEmpty() + "</body></html>");

        int targetIdx = targetBox.getSelectedIndex();
        int scanIdx = scanModeBox.getSelectedIndex();
        int tileIdx = tileSizeBox.getSelectedIndex();
        int copyIdx = copyFormatBox.getSelectedIndex();
        updatingFields = true;
        try {
            targetBox.removeAllItems();
            for (BiomePreset preset : targetPresets) targetBox.addItem(preset.label());
            targetBox.setSelectedIndex(Math.max(0, targetIdx));

            scanModeBox.removeAllItems();
            scanModeBox.addItem(I18n.scanAuto());
            scanModeBox.addItem(I18n.scanFull());
            scanModeBox.addItem(I18n.scanTiled());
            scanModeBox.setSelectedIndex(Math.max(0, scanIdx));

            tileSizeBox.removeAllItems();
            tileSizeBox.addItem(I18n.tileAuto());
            for (String s : new String[] {"128", "256", "512", "1024", "2048"}) {
                tileSizeBox.addItem(s);
            }
            tileSizeBox.setSelectedIndex(Math.max(0, tileIdx));

            String threadSel = selectedText(threadsBox);
            boolean wasAuto = threadSel != null
                    && (threadSel.startsWith("自动") || threadSel.toLowerCase(Locale.ROOT)
                    .startsWith("auto"));
            loadThreadsItems();
            if (!wasAuto && threadSel != null) threadsBox.setSelectedItem(threadSel);

            loadRadiusItems();

            copyFormatBox.removeAllItems();
            for (String f : I18n.copyFormats()) copyFormatBox.addItem(f);
            copyFormatBox.setSelectedIndex(Math.max(0, copyIdx));
        } finally {
            updatingFields = false;
        }

        rebuildRecentBox();
        tableModel.fireTableStructureChanged();
        installNumericRenderers();
        tableModel.fireTableDataChanged();
        updateTargetFieldState();
        updateScanFieldState();
        updateActionButtons();
        if (!searching) {
            timeLabel.setText("");
            if (lastResults.isEmpty()) {
                progressBar.setCaption("");
            } else {
                progressBar.setCaption("100% · " + I18n.stageDone());
            }
            if (!NativeBridge.isLoaded()) {
                statusLabel.setText(I18n.nativeMissing(NativeBridge.getLoadError()));
            } else if (!lastResults.isEmpty()) {
                statusLabel.setText(I18n.doneFound(lastResults.size()));
            } else {
                statusLabel.setText(I18n.ready());
            }
        }
        if (!lastResults.isEmpty()) {
            countPill.setText(I18n.resultsFound(lastResults.size()));
            countPill.setVisible(true);
            boolean tiled = lastPlan != null ? lastPlan.tiled() : runner.isTiled();
            modePill.setText(tiled ? I18n.planTiled() : I18n.planMonolithic());
            modePill.setTone(tiled ? FluentTheme.WARN_SOFT : FluentTheme.ACCENT_SOFT,
                    FluentTheme.BORDER, tiled ? FluentTheme.WARN : FluentTheme.ACCENT);
            modePill.setVisible(true);
        }
    }

    private void updateTargetFieldState() {
        boolean custom = selectedPreset().isCustom();
        customBiomesLabel.setVisible(custom);
        customBiomesField.setVisible(custom);
        customBiomesField.setEnabled(custom && !searching);
        if (custom && customBiomesField.getText().isBlank()) {
            customBiomesField.setText(prefs.customBiomes);
        }
    }

    private void updateScanFieldState() {
        boolean tiled = scanModeBox.getSelectedIndex() == 2;
        tileSizeBox.setEnabled(!searching && tiled);
    }

    // ------------------------------------------------------------------
    // prefs
    // ------------------------------------------------------------------

    private void loadPrefsIntoUi() {
        updatingFields = true;
        try {
            seedBox.setSelectedItem(Long.toString(prefs.seed));
            if (!selectComboItem(versionBox, prefs.version)) {
                versionBox.addItem(prefs.version);
                versionBox.setSelectedItem(prefs.version);
            }
            centerXField.setText(Integer.toString(prefs.centerX));
            centerZField.setText(Integer.toString(prefs.centerZ));
            radiusBox.setSelectedItem(Integer.toString(prefs.radius));
            int presetIndex = BiomePreset.indexOfId(prefs.targetPreset);
            if (presetIndex < 0) presetIndex = BiomePreset.indexFromLegacyMode(prefs.targetMode);
            targetBox.setSelectedIndex(Math.max(0, presetIndex));
            customBiomesField.setText(prefs.customBiomes == null ? "14" : prefs.customBiomes);
            minAreaSpinner.setValue(Math.max(0L, Math.min(1_000_000_000L, prefs.minArea)));
            maxResultsSpinner.setValue(Math.max(0, Math.min(500, prefs.maxResults)));
            selectComboItem(scanModeBox, null);
            scanModeBox.setSelectedIndex(scanPrefToCombo(prefs.scanMode));
            tileSizeBox.setSelectedIndex(prefs.tileSize <= 0 ? 0 : tileIndexFor(prefs.tileSize));
            refineCheck.setSelected(prefs.refine);
            if (prefs.threads <= 0) threadsBox.setSelectedIndex(0);
            else threadsBox.setSelectedItem(Integer.toString(prefs.threads));
            selectComboItem(langBox, I18n.langDisplayName(I18n.get()));
            recentEntries = new ArrayList<>(prefs.recent());
            rebuildRecentBox();
        } finally {
            updatingFields = false;
        }
        updateTargetFieldState();
        updateScanFieldState();
    }

    private static boolean selectComboItem(JComboBox<String> box, String value) {
        if (value == null) return false;
        for (int i = 0; i < box.getItemCount(); i++) {
            if (value.equals(box.getItemAt(i))) {
                box.setSelectedIndex(i);
                return true;
            }
        }
        return false;
    }

    private static int tileIndexFor(int tileSize) {
        String[] values = {"128", "256", "512", "1024", "2048"};
        for (int i = 0; i < values.length; i++) {
            if (Integer.parseInt(values[i]) == tileSize) return i + 1;
        }
        return 0;
    }

    private void rebuildRecentBox() {
        updatingFields = true;
        try {
            recentBox.removeAllItems();
            if (recentEntries.isEmpty()) {
                recentBox.addItem(I18n.recentEmpty());
                recentBox.setEnabled(false);
                clearRecentBtn.setEnabled(false);
            } else {
                recentBox.setEnabled(true);
                clearRecentBtn.setEnabled(true);
                for (String encoded : recentEntries) {
                    recentBox.addItem(recentLabel(encoded));
                }
            }
        } finally {
            updatingFields = false;
        }
    }

    private static String recentLabel(String encoded) {
        String[] p = encoded.split("\\|", -1);
        if (p.length < 6) return encoded;
        String seed = p[0];
        String version = p[1];
        String cx = p[2];
        String cz = p[3];
        String radius = p[4];
        return seed + " · " + version + " · (" + cx + ", " + cz + ") · ±" + radius;
    }

    private void applyRecentSelection() {
        if (updatingFields) return;
        int idx = recentBox.getSelectedIndex();
        if (idx < 0 || idx >= recentEntries.size()) return;
        String[] p = recentEntries.get(idx).split("\\|", -1);
        if (p.length < 6) return;
        updatingFields = true;
        try {
            seedBox.setSelectedItem(p[0]);
            if (!selectComboItem(versionBox, p[1])) {
                versionBox.addItem(p[1]);
                versionBox.setSelectedItem(p[1]);
            }
            centerXField.setText(p[2]);
            centerZField.setText(p[3]);
            radiusBox.setSelectedItem(p[4]);
            int presetIdx;
            if (p.length >= 8) {
                presetIdx = BiomePreset.indexOfId(p[7]);
                if (presetIdx < 0) presetIdx = 0;
            } else {
                presetIdx = BiomePreset.indexFromLegacyMode(parseIntSafe(p[5], 0));
            }
            targetBox.setSelectedIndex(presetIdx);
            if (p.length >= 7) customBiomesField.setText(p[6]);
        } finally {
            updatingFields = false;
        }
        updateTargetFieldState();
        refreshPlan();
    }

    private void savePrefsFromUi() {
        try {
            long seed = parseSeedLenient();
            if (seed != 0 || !seedBox.isEditable()) prefs.seed = seed;
            Object v = versionBox.getSelectedItem();
            if (v != null) prefs.version = String.valueOf(v);
            prefs.centerX = parseIntSafe(centerXField.getText().trim(), prefs.centerX);
            prefs.centerZ = parseIntSafe(centerZField.getText().trim(), prefs.centerZ);
            prefs.radius = parseIntSafe(String.valueOf(radiusBox.isEditable()
                    ? radiusBox.getEditor().getItem() : radiusBox.getSelectedItem()), prefs.radius);
            prefs.minArea = ((Number) minAreaSpinner.getValue()).longValue();
            prefs.maxResults = ((Number) maxResultsSpinner.getValue()).intValue();
            prefs.threads = resolveThreads(true);
            prefs.scanMode = scanComboToPref(scanModeBox.getSelectedIndex());
            prefs.tileSize = resolveTileSize(false);
            prefs.targetPreset = selectedPreset().id();
            prefs.targetMode = BiomePreset.legacyModeFor(prefs.targetPreset);
            prefs.customBiomes = customBiomesField.getText().trim();
            prefs.refine = refineCheck.isSelected();
            prefs.language = I18n.isZh() ? "zh" : "en";
            prefs.save();
        } catch (RuntimeException ignored) {
            // best effort only
        }
    }

    private long parseSeedLenient() {
        String s = selectedText(seedBox);
        if (s == null) return prefs.seed;
        if (s.startsWith("#")) s = s.substring(1);
        try { return Long.parseLong(s.trim()); } catch (Exception e) { return prefs.seed; }
    }

    // ------------------------------------------------------------------
    // validation / plan
    // ------------------------------------------------------------------

    private long parseSeedOrThrow() {
        String seedText = selectedText(seedBox);
        if (seedText == null) throw new NumberFormatException("empty");
        if (seedText.startsWith("#")) seedText = seedText.substring(1);
        return Long.parseLong(seedText.trim());
    }

    private int parseCenterOrThrow(JTextField field) {
        String s = field.getText().trim();
        if (!s.matches("[+-]?\\d+")) throw new NumberFormatException(s);
        long v = Long.parseLong(s);
        if (v < -30_000_000L || v > 30_000_000L) {
            throw new NumberFormatException(s);
        }
        return (int) v;
    }

    private int parseRadiusOrThrow() {
        String s = selectedText(radiusBox);
        if (s == null || !s.matches("[+-]?\\d+")) {
            throw new NumberFormatException(s == null ? "" : s);
        }
        return Integer.parseInt(s);
    }

    private int resolveThreads(boolean clamp) {
        String s = selectedText(threadsBox);
        if (s == null || s.isBlank() || s.startsWith("自动")
                || s.toLowerCase(Locale.ROOT).startsWith("auto")) {
            return 0; // native auto (physical cores)
        }
        try {
            int n = Integer.parseInt(s);
            if (n < 1) return 0;
            return clamp ? Math.min(256, n) : n;
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private int resolveTileSize(boolean clamp) {
        int idx = tileSizeBox.getSelectedIndex();
        if (idx <= 0) return 0;
        String s = String.valueOf(tileSizeBox.getItemAt(idx));
        try {
            int n = Integer.parseInt(s);
            if (n < 64) return 0;
            return clamp ? Math.min(2048, n) : n;
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private int resolveUseTiles() {
        return switch (scanModeBox.getSelectedIndex()) {
            case 1 -> 0;
            case 2 -> 1;
            default -> 2;
        };
    }

    /** Combo order is Auto / Full / Tiled; preference order is Full=0, Tiled=1, Auto=2. */
    private static int scanComboToPref(int index) {
        return switch (index) {
            case 1 -> 0;
            case 2 -> 1;
            default -> 2;
        };
    }

    private static int scanPrefToCombo(int mode) {
        return switch (mode) {
            case 0 -> 1;
            case 1 -> 2;
            default -> 0;
        };
    }

    /** @return biome ids for the selected preset, or parsed custom ids. */
    private int[] resolveBiomesOrThrow() {
        BiomePreset preset = selectedPreset();
        if (!preset.isCustom()) {
            return preset.ids();
        }
        String raw = customBiomesField.getText().trim();
        if (raw.isEmpty()) throw new NumberFormatException("empty");
        String[] parts = raw.split("[,\\s]+");
        int[] ids = new int[parts.length];
        int n = 0;
        for (String part : parts) {
            if (part.isBlank()) continue;
            int id;
            try { id = Integer.parseInt(part.trim()); }
            catch (NumberFormatException e) { throw e; }
            if (id < 0 || id > 255) throw new NumberFormatException(part);
            boolean dup = false;
            for (int i = 0; i < n; i++) if (ids[i] == id) dup = true;
            if (!dup) ids[n++] = id;
        }
        if (n == 0) throw new NumberFormatException("empty");
        return java.util.Arrays.copyOf(ids, n);
    }

    private void refreshPlan() {
        if (updatingFields) return;
        if (!NativeBridge.isLoaded()) {
            showPlanWarning(I18n.nativeMissing(NativeBridge.getLoadError()));
            return;
        }
        String version = versionBox.getSelectedItem() == null ? "1.21.3"
                : String.valueOf(versionBox.getSelectedItem());
        int cx, cz, radius;
        try {
            cx = parseCenterOrThrow(centerXField);
            cz = parseCenterOrThrow(centerZField);
            radius = parseRadiusOrThrow();
        } catch (NumberFormatException e) {
            showPlanWarning(I18n.planNone());
            return;
        }
        try {
            resolveBiomesOrThrow();
        } catch (NumberFormatException e) {
            showPlanWarning(I18n.errBiomes(customBiomesField.getText().trim())
                    .replace('\n', ' '));
            return;
        }
        NativeBridge.SearchPlan plan = NativeBridge.plan(
                version, cx, cz, radius, 16, resolveUseTiles(),
                resolveTileSize(true), resolveThreads(true));
        if (plan == null) {
            showPlanWarning(I18n.planInvalid(firstLine(safeLastError())));
            lastPlan = null;
            return;
        }
        lastPlan = plan;
        String mode = plan.tiled() ? I18n.planTiled() : I18n.planMonolithic();
        String summary = I18n.planSummary(mode, plan.gridText(), plan.estMb(), plan.estSamples());
        planLabel.setText("<html><body style='width:350px'>" + escapeHtml(summary)
                + "</body></html>");
        boolean heavy = plan.estMb() > 256 || plan.estSamples() > 100_000_000L;
        planLabel.setForeground(heavy ? FluentTheme.WARN : FluentTheme.MUTED);
        updateSearchButtonState();
    }

    private void showPlanWarning(String text) {
        planLabel.setText("<html><body style='width:330px'>" + escapeHtml(text) + "</body></html>");
        planLabel.setForeground(FluentTheme.WARN);
        lastPlan = null;
        updateSearchButtonState();
    }

    private void updateSearchButtonState() {
        if (searching) {
            searchBtn.setEnabled(false);
            return;
        }
        searchBtn.setEnabled(NativeBridge.isLoaded());
    }

    private void startSearch() {
        long seed;
        try {
            seed = parseSeedOrThrow();
        } catch (NumberFormatException ex) {
            JOptionPane.showMessageDialog(this,
                    I18n.invalidSeed(selectedText(seedBox)),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        int centerX, centerZ;
        try {
            centerX = parseCenterOrThrow(centerXField);
            centerZ = parseCenterOrThrow(centerZField);
        } catch (NumberFormatException ex) {
            String rawX = centerXField.getText().trim();
            String rawZ = centerZField.getText().trim();
            boolean integer = rawX.matches("[+-]?\\d+") && rawZ.matches("[+-]?\\d+");
            JOptionPane.showMessageDialog(this,
                    integer ? I18n.errCenterBorder(30_000_000, rawX + ", " + rawZ)
                            : I18n.errCenterInteger(rawX + ", " + rawZ),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        int radius;
        try {
            radius = parseRadiusOrThrow();
        } catch (NumberFormatException ex) {
            JOptionPane.showMessageDialog(this,
                    I18n.errRadiusInteger(selectedText(radiusBox)),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        RadiusValidator.Result rr = RadiusValidator.parse(String.valueOf(radius));
        if (!rr.ok()) {
            String msg = switch (rr.error) {
                case TOO_SMALL -> I18n.errRadiusTooSmall(RadiusValidator.MIN_RADIUS,
                        selectedText(radiusBox));
                case BEYOND_WORLD_BORDER -> I18n.errRadiusWorldBorder(
                        RadiusValidator.MAX_RADIUS, selectedText(radiusBox));
                default -> I18n.errRadiusInteger(selectedText(radiusBox));
            };
            JOptionPane.showMessageDialog(this, msg, I18n.inputErrorTitle(),
                    JOptionPane.ERROR_MESSAGE);
            return;
        }
        radius = rr.radius;
        int nativeMax = maxRadiusScale16();
        if (nativeMax > 0 && radius > nativeMax) {
            JOptionPane.showMessageDialog(this,
                    I18n.errRadiusNativeLimit(nativeMax, 16, selectedText(radiusBox)),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (Math.abs((long) centerX) + radius > 30_000_000L
                || Math.abs((long) centerZ) + radius > 30_000_000L) {
            JOptionPane.showMessageDialog(this, I18n.errBoxBorder(radius, centerX, centerZ),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        radiusBox.setSelectedItem(Integer.toString(radius));

        int[] biomes;
        try {
            biomes = resolveBiomesOrThrow();
        } catch (NumberFormatException ex) {
            JOptionPane.showMessageDialog(this, I18n.errBiomes(customBiomesField.getText().trim()),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        String version = String.valueOf(versionBox.getSelectedItem());
        int useTiles = resolveUseTiles();
        int tileSize = resolveTileSize(true);
        int threads = resolveThreads(true);

        NativeBridge.SearchPlan plan = NativeBridge.plan(version, centerX, centerZ,
                radius, 16, useTiles, tileSize, threads);
        if (plan == null) {
            showLongError(safeLastError());
            return;
        }
        lastPlan = plan;
        if (plan.estMb() > 256 || plan.estSamples() > 100_000_000L) {
            if (!confirmLargeSearch(plan, radius)) return;
        }

        long minArea = ((Number) minAreaSpinner.getValue()).longValue();
        int maxResults = ((Number) maxResultsSpinner.getValue()).intValue();
        BiomePreset preset = selectedPreset();
        boolean includeShore = "mushroom_shore".equals(preset.id());
        SearchSettings settings = new SearchSettings(
                seed, version, centerX, centerZ, radius, minArea, 16,
                refineCheck.isSelected(), includeShore, threads, maxResults,
                useTiles, tileSize, biomes);
        lastSettings = settings;

        prefs.seed = seed;
        prefs.version = version;
        prefs.centerX = centerX;
        prefs.centerZ = centerZ;
        prefs.radius = radius;
        prefs.minArea = minArea;
        prefs.maxResults = maxResults;
        prefs.threads = threads;
        prefs.scanMode = scanComboToPref(scanModeBox.getSelectedIndex());
        prefs.tileSize = tileSize;
        prefs.targetPreset = preset.id();
        prefs.targetMode = BiomePreset.legacyModeFor(prefs.targetPreset);
        prefs.customBiomes = customBiomesField.getText().trim();
        prefs.refine = refineCheck.isSelected();
        prefs.language = I18n.isZh() ? "zh" : "en";
        prefs.pushRecent(encodeRecent(seed, version, centerX, centerZ, radius,
                preset.id(), customBiomesField.getText().trim()));
        prefs.save();
        recentEntries = new ArrayList<>(prefs.recent());
        rebuildRecentBox();

        lastResults = List.of();
        tableModel.clear();
        searching = true;
        searchStartNanos = System.nanoTime();
        pauseStartNanos = 0L;
        pausedTotalNanos = 0L;
        lastPercent = 0;

        String box = I18n.searchBoxLabel(centerX, centerZ, radius);
        progressBar.setIndeterminate(true);
        progressBar.setCaption(I18n.initializing());
        statusLabel.setText(I18n.initializing() + "  " + box);
        timeLabel.setText("");
        setFormEnabled(false);
        searchBtn.setEnabled(false);
        pauseBtn.setEnabled(true);
        pauseBtn.setText(I18n.pause());
        stopBtn.setEnabled(true);
        modePill.setVisible(false);
        countPill.setVisible(false);
        setCursor(Cursor.getPredefinedCursor(Cursor.WAIT_CURSOR));
        showEmptyState(true, I18n.searchingPercent(0));

        SwingUtilities.invokeLater(() ->
                statusLabel.setText(I18n.startingThreads(threads == 0
                        ? NativeBridge.defaultThreads() : threads) + "  " + box));
        runner.start(settings);
    }

    private boolean confirmLargeSearch(NativeBridge.SearchPlan plan, int radius) {
        String mode = plan.tiled() ? I18n.planTiled() : I18n.planMonolithic();
        String summary = I18n.planSummary(mode, plan.gridText(), plan.estMb(), plan.estSamples());
        String warning = plan.estBytes() > 512L * 1024 * 1024
                ? I18n.warnHugeRadius(plan.estSamples())
                : I18n.warnLargeRadius(radius);
        JTextArea area = new JTextArea(I18n.confirmLargeBody(summary, warning));
        area.setEditable(false);
        area.setLineWrap(true);
        area.setWrapStyleWord(true);
        area.setFont(FluentTheme.bodyFont());
        area.setBackground(FluentTheme.CARD);
        area.setColumns(46);
        area.setRows(8);
        area.setBorder(BorderFactory.createEmptyBorder(8, 8, 8, 8));
        int r = JOptionPane.showConfirmDialog(this, area, I18n.confirmLargeTitle(),
                JOptionPane.OK_CANCEL_OPTION, JOptionPane.INFORMATION_MESSAGE);
        return r == JOptionPane.OK_OPTION;
    }

    private static String encodeRecent(long seed, String version, int cx, int cz,
                                       int radius, String targetPreset, String custom) {
        return seed + "|" + version + "|" + cx + "|" + cz + "|" + radius
                + "|" + BiomePreset.legacyModeFor(targetPreset) + "|"
                + (custom == null ? "" : custom) + "|"
                + (targetPreset == null ? "mushroom" : targetPreset);
    }

    private void togglePause() {
        if (runner.isStopping()) return;
        if (runner.isPaused()) {
            runner.resume();
            if (pauseStartNanos > 0) {
                pausedTotalNanos += System.nanoTime() - pauseStartNanos;
                pauseStartNanos = 0L;
            }
            pauseBtn.setText(I18n.pause());
            statusLabel.setText(I18n.searching());
            updateTiming(lastPercent);
        } else if (searching) {
            runner.pause();
            pauseStartNanos = System.nanoTime();
            pauseBtn.setText(I18n.resume());
            statusLabel.setText(I18n.paused());
            timeLabel.setText(I18n.elapsedOnly(formatElapsed()));
        }
    }

    private String formatElapsed() {
        long nanos = System.nanoTime() - searchStartNanos - pausedTotalNanos;
        if (pauseStartNanos > 0) nanos -= (System.nanoTime() - pauseStartNanos);
        return formatDuration(Math.max(0L, nanos / 1_000_000L));
    }

    private static String formatDuration(long ms) {
        long safe = Math.max(0L, ms);
        if (safe < 1_000L) {
            return String.format(Locale.ROOT, "%.2fs", safe / 1000.0);
        }
        if (safe < 10_000L) {
            return String.format(Locale.ROOT, "%.1fs", safe / 1000.0);
        }
        long totalSec = safe / 1000L;
        long h = totalSec / 3600L;
        long m = (totalSec % 3600L) / 60L;
        long s = totalSec % 60L;
        if (h > 0) return String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s);
        return String.format(Locale.ROOT, "%02d:%02d", m, s);
    }

    private void updateTiming(int percent) {
        if (searchStartNanos == 0L) return;
        long elapsedMs = System.nanoTime() - searchStartNanos - pausedTotalNanos;
        if (pauseStartNanos > 0) elapsedMs -= (System.nanoTime() - pauseStartNanos);
        elapsedMs = Math.max(0L, elapsedMs / 1_000_000L);
        String elapsed = formatDuration(elapsedMs);
        if (percent >= 3 && !runner.isPaused()) {
            long etaMs = (long) ((double) elapsedMs * (100 - percent) / percent);
            timeLabel.setText(I18n.elapsedEta(elapsed, formatDuration(etaMs)));
        } else {
            timeLabel.setText(I18n.elapsedOnly(elapsed));
        }
    }

    private void setFormEnabled(boolean enabled) {
        seedBox.setEnabled(enabled);
        versionBox.setEnabled(enabled);
        centerXField.setEnabled(enabled);
        centerZField.setEnabled(enabled);
        radiusBox.setEnabled(enabled);
        targetBox.setEnabled(enabled);
        customBiomesField.setEnabled(enabled && selectedPreset().isCustom());
        recentBox.setEnabled(enabled && !recentEntries.isEmpty());
        clearRecentBtn.setEnabled(enabled && !recentEntries.isEmpty());
        maxResultsSpinner.setEnabled(enabled);
        minAreaSpinner.setEnabled(enabled);
        refineCheck.setEnabled(enabled);
        threadsBox.setEnabled(enabled);
        scanModeBox.setEnabled(enabled);
        tileSizeBox.setEnabled(enabled && scanModeBox.getSelectedIndex() == 2);
        langBox.setEnabled(true);
        copyFormatBox.setEnabled(true);
    }

    private void updateActionButtons() {
        boolean hasResults = !lastResults.isEmpty();
        copyBtn.setEnabled(hasResults && !searching);
        exportCsvBtn.setEnabled(hasResults && !searching);
        exportJsonBtn.setEnabled(hasResults && !searching);
        if (!searching) {
            searchBtn.setEnabled(NativeBridge.isLoaded());
            pauseBtn.setEnabled(false);
            pauseBtn.setText(I18n.pause());
            stopBtn.setEnabled(false);
            setCursor(Cursor.getDefaultCursor());
            updateSearchButtonState();
        }
        countPill.setText(I18n.resultsFound(lastResults.size()));
        countPill.setVisible(!lastResults.isEmpty());
    }

    private void finishUiState() {
        searching = false;
        progressBar.setIndeterminate(false);
        setFormEnabled(true);
        updateActionButtons();
        updateTargetFieldState();
        updateScanFieldState();
        refreshPlan();
    }

    // ------------------------------------------------------------------
    // SearchRunner.Listener
    // ------------------------------------------------------------------

    @Override
    public void onProgress(int percent, int found, String stage) {
        if (stage == null) stage = "";

        if (SearchRunner.STAGE_STOPPING.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setCaption(I18n.stopping());
            statusLabel.setText(I18n.stopping());
            pauseBtn.setEnabled(false);
            stopBtn.setEnabled(false);
            return;
        }
        if (SearchRunner.STAGE_PAUSED.equals(stage)) {
            progressBar.setIndeterminate(false);
            if (percent >= 0) progressBar.setValue(percent);
            progressBar.setCaption(I18n.paused());
            statusLabel.setText(I18n.paused());
            pauseBtn.setText(I18n.resume());
            return;
        }
        if (SearchRunner.STAGE_INIT.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setCaption(I18n.initializing());
            statusLabel.setText(I18n.initializing());
            return;
        }
        if (SearchRunner.STAGE_STARTING.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setCaption(I18n.startingThreads(
                    resolveThreads(true) == 0 ? NativeBridge.defaultThreads()
                            : resolveThreads(true)));
            return;
        }

        String localized = localizeStage(stage);
        if (percent >= 0) {
            lastPercent = Math.max(lastPercent, percent);
            progressBar.setIndeterminate(false);
            progressBar.setValue(percent);
            String mode = runner.isTiled() ? I18n.planTiled() : I18n.planMonolithic();
            progressBar.setCaption(percent + "% · " + localized + " · " + mode);
            updateTiming(percent);
            statusLabel.setText(I18n.progressLine(percent, found, localized));
        } else {
            progressBar.setIndeterminate(true);
            progressBar.setCaption(localized);
            if (SearchRunner.STAGE_SCAN.equals(stage)) {
                statusLabel.setText(I18n.scanStarted());
            } else {
                statusLabel.setText(localized);
            }
        }
    }

    private String localizeStage(String stage) {
        if (stage == null) return "";
        return switch (stage) {
            case SearchRunner.STAGE_SCAN -> I18n.stageScan();
            case SearchRunner.STAGE_TILE -> I18n.stageTile();
            case SearchRunner.STAGE_MERGE -> I18n.stageMerge();
            case SearchRunner.STAGE_CONNECT -> I18n.stageConnect();
            case SearchRunner.STAGE_REFINE -> I18n.stageRefine();
            case SearchRunner.STAGE_DONE -> I18n.stageDone();
            case SearchRunner.STAGE_INIT -> I18n.stageInit();
            case SearchRunner.STAGE_PAUSED -> I18n.paused();
            case SearchRunner.STAGE_STOPPING -> I18n.stopping();
            default -> stage;
        };
    }

    @Override
    public void onCompleted(List<SearchResult> results) {
        lastResults = results;
        tableModel.setResults(results);
        progressBar.setIndeterminate(false);
        progressBar.setValue(100);
        progressBar.setCaption("100% · " + I18n.stageDone());
        long ms = runner.lastDurationMs();
        String elapsed = formatDuration(ms);
        statusLabel.setText(I18n.doneFoundTimed(results.size(), elapsed));
        timeLabel.setText(I18n.elapsedOnly(elapsed));
        showEmptyState(results.isEmpty(), results.isEmpty() ? I18n.resultsNone() : null);
        if (!results.isEmpty()) {
            resultTable.setRowSelectionInterval(0, 0);
            resultTable.scrollRectToVisible(resultTable.getCellRect(0, 0, true));
            applyDefaultSort();
        }
        boolean tiled = lastPlan != null ? lastPlan.tiled() : runner.isTiled();
        modePill.setText(tiled ? I18n.planTiled() : I18n.planMonolithic());
        modePill.setTone(tiled ? FluentTheme.WARN_SOFT : FluentTheme.ACCENT_SOFT,
                FluentTheme.BORDER, tiled ? FluentTheme.WARN : FluentTheme.ACCENT);
        modePill.setVisible(true);
        finishUiState();
    }

    private void applyDefaultSort() {
        if (resultTable.getRowSorter() instanceof TableRowSorter<?> sorter) {
            @SuppressWarnings("unchecked")
            TableRowSorter<ResultTableModel> typed = (TableRowSorter<ResultTableModel>) sorter;
            typed.setSortKeys(List.of(new RowSorter.SortKey(COL_AREA, SortOrder.DESCENDING)));
        }
    }

    @Override
    public void onCancelled() {
        progressBar.setIndeterminate(false);
        statusLabel.setText(I18n.stopped());
        showEmptyState(true, I18n.stopped());
        finishUiState();
    }

    @Override
    public void onError(String message) {
        progressBar.setIndeterminate(false);
        statusLabel.setText(I18n.errTitle() + ": " + firstLine(message));
        showEmptyState(true, I18n.errTitle() + ": " + firstLine(message));
        finishUiState();
        showLongError(message);
    }

    private void showEmptyState(boolean empty, String text) {
        if (text != null) {
            emptyLabel.setText("<html><body style='width:420px;text-align:center'>"
                    + escapeHtml(text) + "</body></html>");
        }
        if (resultsCenter != null && resultsCenter.getLayout() instanceof java.awt.CardLayout cl) {
            cl.show(resultsCenter, empty ? "empty" : "table");
        }
    }

    // ------------------------------------------------------------------
    // results / clipboard / export
    // ------------------------------------------------------------------

    private void copySelected() {
        if (lastResults.isEmpty()) {
            statusLabel.setText(I18n.nothingToCopy());
            return;
        }
        int viewRow = resultTable.getSelectedRow();
        SearchResult r;
        if (viewRow < 0) {
            r = lastResults.get(0);
            if (resultTable.getRowCount() > 0) resultTable.setRowSelectionInterval(0, 0);
        } else {
            int modelRow = resultTable.convertRowIndexToModel(viewRow);
            r = tableModel.get(modelRow);
        }
        String text = formatCoordinate(r);
        Toolkit.getDefaultToolkit().getSystemClipboard()
                .setContents(new StringSelection(text), null);
        statusLabel.setText(I18n.copied(text));
    }

    private String formatCoordinate(SearchResult r) {
        String fmt = (String) copyFormatBox.getSelectedItem();
        if (fmt != null && fmt.contains("/tp")) {
            return "/tp @s " + r.centerX + " ~ " + r.centerZ;
        }
        if (fmt != null && fmt.startsWith("X:")) {
            return "X: " + r.centerX + "  Z: " + r.centerZ;
        }
        return r.centerX + " " + r.centerZ;
    }

    private void exportResults(boolean csv) {
        if (lastResults.isEmpty()) {
            JOptionPane.showMessageDialog(this, I18n.nothingToExport(),
                    I18n.exportTitle(), JOptionPane.INFORMATION_MESSAGE);
            return;
        }
        JFileChooser chooser = new JFileChooser();
        chooser.setSelectedFile(new File(csv ? "gmif-results.csv" : "gmif-results.json"));
        if (chooser.showSaveDialog(this) != JFileChooser.APPROVE_OPTION) return;
        File file = chooser.getSelectedFile();
        try {
            if (csv) writeCsv(file);
            else writeJson(file);
            statusLabel.setText(I18n.exportOk(file.getAbsolutePath()));
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, ex.getMessage(),
                    I18n.exportFailedTitle(), JOptionPane.ERROR_MESSAGE);
        }
    }

    private SearchSettings exportSettings() {
        return lastSettings != null ? lastSettings : new SearchSettings(
                parseSeedLenient(), String.valueOf(versionBox.getSelectedItem()),
                0, 0, 1000, 0, 16, false, false, 0, 10, 2, 0, null);
    }

    private void writeCsv(File file) throws IOException {
        SearchSettings s = exportSettings();
        try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
            w.println("seed,version,center_x,center_z,radius,rank,"
                    + "island_center_x,island_center_z,min_x,max_x,min_z,max_z,"
                    + "x_span,z_span,diagonal_span,area_estimated,area_km2,"
                    + "area_precision,samples,compactness,perimeter,distance");
            int i = 0;
            for (SearchResult r : lastResults) {
                i++;
                w.printf(Locale.ROOT,
                        "%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d,%.6f,"
                                + "%d,%d,%.4f,%d,%.2f%n",
                        s.seed, s.version, s.centerX, s.centerZ, s.radius, i,
                        r.centerX, r.centerZ, r.minX, r.maxX, r.minZ, r.maxZ,
                        r.width, r.height, r.diagonalSpan,
                        r.area, r.areaKm2(), r.areaPrecision, r.samples,
                        r.compactness, r.perimeter, r.distance);
            }
        }
    }

    private void writeJson(File file) throws IOException {
        SearchSettings s = exportSettings();
        try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
            w.println("{");
            w.printf("  \"seed\": %d,%n", s.seed);
            w.printf("  \"version\": \"%s\",%n", s.version);
            w.printf("  \"centerX\": %d,%n", s.centerX);
            w.printf("  \"centerZ\": %d,%n", s.centerZ);
            w.printf("  \"radius\": %d,%n", s.radius);
            w.printf("  \"minArea\": %d,%n", s.minArea);
            w.printf("  \"refine\": %s,%n", s.refine);
            boolean tiled = lastPlan != null ? lastPlan.tiled() : runner.isTiled();
            w.printf("  \"mode\": \"%s\",%n", tiled ? "tiled" : "monolithic");
            w.printf("  \"durationMs\": %d,%n", runner.lastDurationMs());
            w.printf("  \"count\": %d,%n", lastResults.size());
            w.println("  \"islands\": [");
            int i = 0;
            for (SearchResult r : lastResults) {
                i++;
                w.printf(Locale.ROOT,
                        "    {\"rank\": %d, \"centerX\": %d, \"centerZ\": %d, "
                                + "\"minX\": %d, \"maxX\": %d, \"minZ\": %d, \"maxZ\": %d, "
                                + "\"xSpan\": %d, \"zSpan\": %d, \"diagonalSpan\": %.2f, "
                                + "\"areaEstimated\": %d, \"areaKm2\": %.6f, "
                                + "\"areaPrecision\": %d, \"samples\": %d, "
                                + "\"compactness\": %.4f, \"perimeter\": %d, "
                                + "\"distance\": %.2f}%s%n",
                        i, r.centerX, r.centerZ, r.minX, r.maxX, r.minZ, r.maxZ,
                        r.width, r.height, r.diagonalSpan,
                        r.area, r.areaKm2(), r.areaPrecision, r.samples,
                        r.compactness, r.perimeter, r.distance,
                        i < lastResults.size() ? "," : "");
            }
            w.println("  ]");
            w.println("}");
        }
    }

    // ------------------------------------------------------------------
    // misc
    // ------------------------------------------------------------------

    private static String firstLine(String s) {
        if (s == null) return "";
        int i = s.indexOf('\n');
        return i < 0 ? s : s.substring(0, i);
    }

    private static int parseIntSafe(String s, int fallback) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return fallback; }
    }

    private static String escapeHtml(String s) {
        if (s == null) return "";
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    private static String safeLastError() {
        try {
            int code = NativeBridge.nativeGetLastErrorCode();
            String msg = NativeBridge.nativeGetLastError();
            return I18n.errNative(code, msg);
        } catch (UnsatisfiedLinkError e) {
            return I18n.isZh()
                    ? ("原生库版本过期（缺少 " + e.getMessage()
                    + "）。请执行 .\\build.ps1 后重启程序。")
                    : ("Native library is out of date (missing " + e.getMessage()
                    + "). Rebuild with .\\build.ps1 and restart the app.");
        } catch (Throwable t) {
            String m = t.getMessage();
            return (m == null || m.isBlank()) ? "Native search failed." : "Native search failed: " + m;
        }
    }

    private void showLongError(String message) {
        JTextArea area = new JTextArea(message == null ? "" : message);
        area.setEditable(false);
        area.setLineWrap(true);
        area.setWrapStyleWord(true);
        area.setFont(FluentTheme.bodyFont());
        area.setBackground(FluentTheme.CARD);
        area.setColumns(56);
        area.setRows(Math.min(16, Math.max(6, 2 + countLines(message))));
        area.setCaretPosition(0);
        JScrollPane sp = new JScrollPane(area);
        sp.setPreferredSize(new Dimension(580, 240));
        JOptionPane.showMessageDialog(this, sp, I18n.errTitle(), JOptionPane.ERROR_MESSAGE);
    }

    private static int countLines(String s) {
        if (s == null) return 0;
        int n = 1;
        for (int i = 0; i < s.length(); i++) if (s.charAt(i) == '\n') n++;
        return n;
    }

    /** Table model: numeric column classes keep sorting correct. */
    private final class ResultTableModel extends AbstractTableModel {
        private List<SearchResult> data = new ArrayList<>();

        void clear() {
            data = new ArrayList<>();
            fireTableDataChanged();
        }

        void setResults(List<SearchResult> results) {
            data = new ArrayList<>(results);
            fireTableDataChanged();
        }

        SearchResult get(int row) { return data.get(row); }

        @Override public int getRowCount() { return data.size(); }
        @Override public int getColumnCount() { return I18n.tableColumns().length; }
        @Override public String getColumnName(int c) { return I18n.tableColumns()[c]; }

        @Override
        public Class<?> getColumnClass(int col) {
            return switch (col) {
                case COL_AREA, COL_DIST -> Long.class;
                case COL_KM2, COL_COMPACT -> Double.class;
                case COL_RANK -> Integer.class;
                default -> String.class;
            };
        }

        @Override
        public Object getValueAt(int row, int col) {
            SearchResult r = data.get(row);
            return switch (col) {
                case COL_RANK -> row + 1;
                case 1 -> r.centerX;
                case 2 -> r.centerZ;
                case COL_AREA -> r.area;
                case COL_KM2 -> r.areaKm2();
                case 5 -> r.width + " × " + r.height;
                case COL_DIST -> (long) r.distance;
                case COL_COMPACT -> r.compactness;
                case COL_PRECISION -> precisionLabel(r.areaPrecision);
                default -> "";
            };
        }
    }

    private static String precisionLabel(int p) {
        return switch (p) {
            case 2 -> "scale 1";
            case 1 -> "scale 4";
            default -> I18n.isZh() ? "粗略" : "coarse";
        };
    }
}
