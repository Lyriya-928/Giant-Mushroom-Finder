package dev.sakuhime.mushroomfinder;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JProgressBar;
import javax.swing.JScrollPane;
import javax.swing.JSpinner;
import javax.swing.JTable;
import javax.swing.JTextField;
import javax.swing.ListSelectionModel;
import javax.swing.SpinnerNumberModel;
import javax.swing.table.AbstractTableModel;
import java.awt.BorderLayout;
import java.awt.Cursor;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.Toolkit;
import java.awt.datatransfer.StringSelection;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Main Swing window.
 *
 * Search lifecycle pattern inspired by SunnySlopes FortressFinderGUI.
 * No source code copied.
 */
public final class MainFrame extends JFrame implements SearchRunner.Listener {

    // Basic
    private final JTextField seedField = new JTextField("262", 20);
    private final JComboBox<String> versionBox = new JComboBox<>();
    private final JComboBox<String> radiusBox = new JComboBox<>();
    private final JSpinner maxResultsSpinner =
            new JSpinner(new SpinnerNumberModel(10, 1, 500, 1));

    // Advanced
    private final JSpinner minAreaSpinner =
            new JSpinner(new SpinnerNumberModel(0L, 0L, 100_000_000L, 1000L));
    private final JCheckBox refineCheck = new JCheckBox();
    private final JCheckBox shoreCheck = new JCheckBox();
    private final JComboBox<String> threadsBox = new JComboBox<>();
    private final JComboBox<String> langBox = new JComboBox<>();
    private final JComboBox<String> copyFormatBox = new JComboBox<>();

    private final JButton searchBtn = new JButton();
    private final JButton pauseBtn = new JButton();
    private final JButton stopBtn = new JButton();
    private final JButton copyBtn = new JButton();
    private final JButton exportCsvBtn = new JButton();
    private final JButton exportJsonBtn = new JButton();

    private final JLabel seedLabel = new JLabel();
    private final JLabel versionLabel = new JLabel();
    private final JLabel radiusLabel = new JLabel();
    private final JLabel maxResultsLabel = new JLabel();
    private final JLabel minAreaLabel = new JLabel();
    private final JLabel threadsLabel = new JLabel();
    private final JLabel langLabel = new JLabel();
    private final JLabel copyFormatLabel = new JLabel();
    private final JLabel radiusHintLabel = new JLabel();

    private final JProgressBar progressBar = new JProgressBar(0, 100);
    private final JLabel statusLabel = new JLabel();
    private final ResultTableModel tableModel = new ResultTableModel();
    private final JTable resultTable = new JTable(tableModel);
    private final JScrollPane resultScroll = new JScrollPane(resultTable);

    private final SearchRunner runner = new SearchRunner();
    private List<SearchResult> lastResults = List.of();
    private boolean searching = false;

    public MainFrame() {
        super(I18n.appTitle());
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setMinimumSize(new Dimension(960, 640));

        for (I18n.Lang l : I18n.Lang.values()) {
            langBox.addItem(I18n.langDisplayName(l));
        }
        langBox.setSelectedItem(I18n.langDisplayName(I18n.get()));

        loadVersionItems();
        loadRadiusItems();
        loadThreadsItems();
        for (String f : I18n.copyFormats()) copyFormatBox.addItem(f);

        buildUi();
        wireEvents();
        applyLanguage();
        runner.addListener(this);

        if (!NativeBridge.isLoaded()) {
            statusLabel.setText(I18n.nativeMissing(NativeBridge.getLoadError()));
            searchBtn.setEnabled(false);
        }

        pack();
        setLocationRelativeTo(null);
    }

    private void loadVersionItems() {
        versionBox.removeAllItems();
        String[] versions = NativeBridge.isLoaded()
                ? NativeBridge.nativeKnownVersions()
                : new String[] {"1.21.3", "1.20", "1.18", "1.16.5"};
        for (String v : versions) versionBox.addItem(v);
        versionBox.setSelectedItem("1.21.3");
    }

    private void loadRadiusItems() {
        String prev = (String) radiusBox.getSelectedItem();
        radiusBox.removeAllItems();
        radiusBox.addItem(I18n.radiusHint500());
        radiusBox.addItem(I18n.radiusHint1000());
        radiusBox.addItem(I18n.radiusHint2000());
        radiusBox.addItem(I18n.radiusHint5000());
        radiusBox.setEditable(true);
        if (prev != null) radiusBox.setSelectedItem(prev);
        else radiusBox.setSelectedItem("1000");
    }

    private void loadThreadsItems() {
        String sel = threadsBox.isEditable()
                ? String.valueOf(threadsBox.getEditor().getItem())
                : (String) threadsBox.getSelectedItem();
        threadsBox.setEditable(true);
        threadsBox.removeAllItems();
        threadsBox.addItem("1");
        threadsBox.addItem("2");
        threadsBox.addItem("4");
        threadsBox.addItem("8");
        threadsBox.addItem("16");
        threadsBox.addItem("32");
        threadsBox.addItem(I18n.auto());
        if (sel != null && !sel.isBlank() && !"null".equals(sel)) {
            threadsBox.setSelectedItem(sel);
        } else {
            threadsBox.setSelectedItem(I18n.auto());
        }
    }

    private JPanel labeledRow(JLabel label, java.awt.Component field) {
        JPanel p = new JPanel(new GridBagLayout());
        GridBagConstraints g = new GridBagConstraints();
        g.insets = new Insets(3, 4, 3, 4);
        g.anchor = GridBagConstraints.WEST;
        g.gridx = 0; g.gridy = 0;
        p.add(label, g);
        g.gridx = 1; g.fill = GridBagConstraints.HORIZONTAL; g.weightx = 1;
        p.add(field, g);
        return p;
    }

    private void buildUi() {
        // ---- Basic panel ----
        JPanel basic = new JPanel(new GridBagLayout());
        basic.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createTitledBorder(I18n.basicSettings()),
                BorderFactory.createEmptyBorder(4, 8, 8, 8)));
        GridBagConstraints gc = new GridBagConstraints();
        gc.insets = new Insets(3, 4, 3, 4);
        gc.anchor = GridBagConstraints.WEST;

        gc.gridx = 0; gc.gridy = 0;
        basic.add(seedLabel, gc);
        gc.gridx = 1; gc.gridwidth = 3;
        gc.fill = GridBagConstraints.HORIZONTAL; gc.weightx = 1;
        basic.add(seedField, gc);

        gc.gridx = 0; gc.gridy = 1; gc.gridwidth = 1;
        gc.fill = GridBagConstraints.NONE; gc.weightx = 0;
        basic.add(versionLabel, gc);
        gc.gridx = 1; gc.gridwidth = 3;
        gc.fill = GridBagConstraints.HORIZONTAL; gc.weightx = 1;
        basic.add(versionBox, gc);

        gc.gridx = 0; gc.gridy = 2; gc.gridwidth = 1;
        gc.fill = GridBagConstraints.NONE; gc.weightx = 0;
        basic.add(radiusLabel, gc);
        gc.gridx = 1;
        gc.fill = GridBagConstraints.HORIZONTAL; gc.weightx = 1;
        basic.add(radiusBox, gc);
        gc.gridx = 2; gc.weightx = 0;
        basic.add(radiusHintLabel, gc);
        gc.gridx = 3; gc.weightx = 1;
        gc.fill = GridBagConstraints.NONE;
        basic.add(Box.createHorizontalGlue(), gc);

        gc.gridx = 0; gc.gridy = 3; gc.weightx = 0;
        basic.add(maxResultsLabel, gc);
        gc.gridx = 1;
        gc.fill = GridBagConstraints.NONE;
        basic.add(maxResultsSpinner, gc);

        // ---- Advanced panel ----
        JPanel advanced = new JPanel(new GridBagLayout());
        advanced.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createTitledBorder(I18n.advancedSettings()),
                BorderFactory.createEmptyBorder(4, 8, 8, 8)));
        GridBagConstraints ac = new GridBagConstraints();
        ac.insets = new Insets(3, 4, 3, 4);
        ac.anchor = GridBagConstraints.WEST;

        ac.gridx = 0; ac.gridy = 0;
        advanced.add(minAreaLabel, ac);
        ac.gridx = 1;
        ac.fill = GridBagConstraints.HORIZONTAL; ac.weightx = 1;
        advanced.add(minAreaSpinner, ac);
        ac.gridx = 2; ac.weightx = 0;
        advanced.add(threadsLabel, ac);
        ac.gridx = 3; ac.fill = GridBagConstraints.HORIZONTAL; ac.weightx = 1;
        advanced.add(threadsBox, ac);

        ac.gridx = 1; ac.gridy = 1; ac.gridwidth = 3;
        ac.fill = GridBagConstraints.HORIZONTAL; ac.weightx = 1;
        advanced.add(refineCheck, ac);
        ac.gridy = 2;
        advanced.add(shoreCheck, ac);

        ac.gridx = 0; ac.gridy = 3; ac.gridwidth = 1; ac.weightx = 0;
        ac.fill = GridBagConstraints.NONE;
        advanced.add(langLabel, ac);
        ac.gridx = 1; ac.fill = GridBagConstraints.HORIZONTAL; ac.weightx = 1;
        advanced.add(langBox, ac);
        ac.gridx = 2; ac.weightx = 0; ac.fill = GridBagConstraints.NONE;
        advanced.add(copyFormatLabel, ac);
        ac.gridx = 3; ac.fill = GridBagConstraints.HORIZONTAL; ac.weightx = 1;
        advanced.add(copyFormatBox, ac);

        JPanel north = new JPanel();
        north.setLayout(new BoxLayout(north, BoxLayout.Y_AXIS));
        north.add(basic);
        north.add(advanced);

        // ---- buttons ----
        JPanel buttons = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 4));
        searchBtn.setPreferredSize(new Dimension(90, 30));
        pauseBtn.setPreferredSize(new Dimension(80, 30));
        stopBtn.setPreferredSize(new Dimension(80, 30));
        copyBtn.setPreferredSize(new Dimension(110, 30));
        buttons.add(searchBtn);
        buttons.add(pauseBtn);
        buttons.add(stopBtn);
        buttons.add(copyBtn);
        buttons.add(exportCsvBtn);
        buttons.add(exportJsonBtn);

        progressBar.setStringPainted(true);
        progressBar.setString("0%");
        progressBar.setEnabled(false);

        JPanel progressPanel = new JPanel(new BorderLayout(8, 0));
        progressPanel.setBorder(BorderFactory.createEmptyBorder(4, 10, 4, 10));
        progressPanel.add(progressBar, BorderLayout.CENTER);
        progressPanel.add(statusLabel, BorderLayout.SOUTH);

        resultTable.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        resultTable.setAutoCreateRowSorter(true);
        resultTable.setRowHeight(22);
        resultTable.setFillsViewportHeight(true);
        resultScroll.setBorder(BorderFactory.createTitledBorder(I18n.results()));

        JPanel bottom = new JPanel(new BorderLayout());
        bottom.add(buttons, BorderLayout.NORTH);
        bottom.add(progressPanel, BorderLayout.CENTER);

        getContentPane().setLayout(new BorderLayout());
        getContentPane().add(north, BorderLayout.NORTH);
        getContentPane().add(resultScroll, BorderLayout.CENTER);
        getContentPane().add(bottom, BorderLayout.SOUTH);
    }

    private void wireEvents() {
        searchBtn.addActionListener(e -> startSearch());
        pauseBtn.addActionListener(e -> togglePause());
        stopBtn.addActionListener(e -> runner.stop());
        copyBtn.addActionListener(e -> copySelected());
        exportCsvBtn.addActionListener(e -> exportResults(true));
        exportJsonBtn.addActionListener(e -> exportResults(false));

        langBox.addActionListener(e -> {
            int idx = langBox.getSelectedIndex();
            I18n.Lang next = (idx == 0) ? I18n.Lang.ZH : I18n.Lang.EN;
            if (next != I18n.get()) {
                I18n.set(next);
                applyLanguage();
            }
        });

        resultTable.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() == 2 && !searching) {
                    copySelected();
                }
            }
        });

        resultTable.getSelectionModel().addListSelectionListener(e -> {
            if (!e.getValueIsAdjusting()) updateActionButtons();
        });
    }

    private void applyLanguage() {
        setTitle(I18n.appTitle());
        seedLabel.setText(I18n.seed());
        versionLabel.setText(I18n.version());
        radiusLabel.setText(I18n.radius());
        maxResultsLabel.setText(I18n.maxResults());
        minAreaLabel.setText(I18n.minArea());
        threadsLabel.setText(I18n.threads());
        langLabel.setText(I18n.language());
        copyFormatLabel.setText(I18n.isZh() ? "复制格式:" : "Copy format:");
        radiusHintLabel.setText(I18n.tipRadiusPresets());

        seedField.setToolTipText(I18n.tipSeed());
        seedLabel.setToolTipText(I18n.tipSeed());
        versionBox.setToolTipText(I18n.tipVersion());
        radiusBox.setToolTipText(I18n.tipRadius());
        maxResultsSpinner.setToolTipText(I18n.tipMaxResults());
        minAreaSpinner.setToolTipText(I18n.tipMinArea());
        refineCheck.setToolTipText(I18n.tipRefine());
        shoreCheck.setToolTipText(I18n.tipShore());
        threadsBox.setToolTipText(I18n.threadsEditableTip());
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
        shoreCheck.setText(I18n.includeShore());
        searchBtn.setText(I18n.search());
        pauseBtn.setText(runner.isPaused() ? I18n.resume() : I18n.pause());
        stopBtn.setText(I18n.stop());
        copyBtn.setText(I18n.copyCoords());
        exportCsvBtn.setText(I18n.exportCsv());
        exportJsonBtn.setText(I18n.exportJson());

        // Retitle group borders
        java.awt.Component north = getContentPane().getComponent(0);
        if (north instanceof JPanel p && p.getComponentCount() >= 2) {
            if (p.getComponent(0) instanceof JPanel basic) {
                basic.setBorder(BorderFactory.createCompoundBorder(
                        BorderFactory.createTitledBorder(I18n.basicSettings()),
                        BorderFactory.createEmptyBorder(4, 8, 8, 8)));
            }
            if (p.getComponent(1) instanceof JPanel adv) {
                adv.setBorder(BorderFactory.createCompoundBorder(
                        BorderFactory.createTitledBorder(I18n.advancedSettings()),
                        BorderFactory.createEmptyBorder(4, 8, 8, 8)));
            }
        }
        resultScroll.setBorder(BorderFactory.createTitledBorder(I18n.results()));

        int fmtIdx = copyFormatBox.getSelectedIndex();
        copyFormatBox.removeAllItems();
        for (String f : I18n.copyFormats()) copyFormatBox.addItem(f);
        copyFormatBox.setSelectedIndex(Math.max(0, fmtIdx));

        String prevThread = (String) threadsBox.getSelectedItem();
        boolean wasAuto = prevThread != null
                && (prevThread.equals(I18n.auto())
                    || prevThread.equals("Auto")
                    || prevThread.equals("自动"));
        loadThreadsItems();
        if (wasAuto) threadsBox.setSelectedItem(I18n.auto());

        // radius presets stay numeric — no reload needed except labels
        loadRadiusKeepValue();

        tableModel.fireTableStructureChanged();

        if (!searching) {
            if (!NativeBridge.isLoaded()) {
                statusLabel.setText(I18n.nativeMissing(NativeBridge.getLoadError()));
            } else {
                statusLabel.setText(I18n.ready());
            }
        }
        updateActionButtons();
    }

    private void loadRadiusKeepValue() {
        String prev = (String) radiusBox.getSelectedItem();
        loadRadiusItems();
        if (prev != null) radiusBox.setSelectedItem(prev);
    }

    private void togglePause() {
        if (runner.isStopping()) {
            return;
        }
        if (runner.isPaused()) {
            runner.resume();
            pauseBtn.setText(I18n.pause());
            statusLabel.setText(I18n.searching());
        } else if (searching) {
            runner.pause();
            pauseBtn.setText(I18n.resume());
            statusLabel.setText(I18n.paused());
        }
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

    private void writeCsv(File file) throws IOException {
        long seed = parseSeedOrThrow();
        String version = (String) versionBox.getSelectedItem();
        try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
            w.println("seed,version,rank,center_x,center_z,x_span,z_span,diagonal_span,"
                    + "area_estimated,area_km2,area_precision,compactness,distance");
            int i = 0;
            for (SearchResult r : lastResults) {
                i++;
                w.printf("%d,%s,%d,%d,%d,%d,%d,%.2f,%d,%.6f,%d,%.4f,%.2f%n",
                        seed, version, i, r.centerX, r.centerZ,
                        r.width, r.height, r.diagonalSpan,
                        r.area, r.areaKm2(), r.areaPrecision,
                        r.compactness, r.distance);
            }
        }
    }

    private void writeJson(File file) throws IOException {
        long seed = parseSeedOrThrow();
        String version = (String) versionBox.getSelectedItem();
        try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
            w.println("{");
            w.printf("  \"seed\": %d,%n", seed);
            w.printf("  \"version\": \"%s\",%n", version);
            w.printf("  \"radius\": %d,%n", parseRadiusOrThrow());
            w.printf("  \"count\": %d,%n", lastResults.size());
            w.println("  \"islands\": [");
            int i = 0;
            for (SearchResult r : lastResults) {
                i++;
                w.printf("    {\"rank\": %d, \"centerX\": %d, \"centerZ\": %d, "
                                + "\"xSpan\": %d, \"zSpan\": %d, \"diagonalSpan\": %.2f, "
                                + "\"areaEstimated\": %d, \"areaKm2\": %.6f, "
                                + "\"areaPrecision\": %d, "
                                + "\"compactness\": %.4f, \"distance\": %.2f}%s%n",
                        i, r.centerX, r.centerZ, r.width, r.height, r.diagonalSpan,
                        r.area, r.areaKm2(), r.areaPrecision,
                        r.compactness, r.distance,
                        i < lastResults.size() ? "," : "");
            }
            w.println("  ]");
            w.println("}");
        }
    }

    private long parseSeedOrThrow() {
        String seedText = seedField.getText().trim();
        if (seedText.startsWith("#")) seedText = seedText.substring(1);
        return Long.parseLong(seedText);
    }

    private int parseRadiusOrThrow() {
        Object v = radiusBox.getEditor() != null
                ? radiusBox.getEditor().getItem()
                : radiusBox.getSelectedItem();
        if (v == null) v = radiusBox.getSelectedItem();
        String s = String.valueOf(v).trim();
        if (s.isEmpty()) {
            throw new NumberFormatException("empty");
        }
        // Allow "1000" or "1 000" style; reject decimals and junk.
        if (!s.matches("[+-]?\\d+")) {
            throw new NumberFormatException(s);
        }
        return Integer.parseInt(s);
    }

    /** @return radius in blocks, or -1 if invalid (dialog already shown). */
    private int validateRadiusOrDialog() {
        Object v = radiusBox.getEditor() != null
                ? radiusBox.getEditor().getItem()
                : radiusBox.getSelectedItem();
        if (v == null) v = radiusBox.getSelectedItem();
        String raw = String.valueOf(v).trim();
        RadiusValidator.Result r = RadiusValidator.parse(raw);
        switch (r.error) {
            case NOT_INTEGER -> {
                JOptionPane.showMessageDialog(this,
                        I18n.errRadiusInteger(raw.isEmpty() ? "()" : raw),
                        I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
                return -1;
            }
            case TOO_SMALL -> {
                JOptionPane.showMessageDialog(this,
                        I18n.errRadiusTooSmall(RadiusValidator.MIN_RADIUS, raw),
                        I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
                return -1;
            }
            case BEYOND_WORLD_BORDER -> {
                JOptionPane.showMessageDialog(this,
                        I18n.errRadiusWorldBorder(RadiusValidator.MAX_RADIUS, raw),
                        I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
                return -1;
            }
            default -> {
                return r.radius;
            }
        }
    }

    private void startSearch() {
        long seed;
        try {
            seed = parseSeedOrThrow();
        } catch (NumberFormatException ex) {
            JOptionPane.showMessageDialog(this,
                    I18n.invalidSeed(seedField.getText().trim()),
                    I18n.inputErrorTitle(), JOptionPane.ERROR_MESSAGE);
            return;
        }
        int radius = validateRadiusOrDialog();
        if (radius < 0) return;

        RadiusValidator.Result rr = RadiusValidator.parse(String.valueOf(radius));
        long samples = rr.coarseSamplePoints(16);
        String box = I18n.searchBoxLabel(radius);
        String extra = "";
        if (rr.isHuge()) {
            extra = "  " + I18n.warnHugeRadius(samples);
        } else if (rr.isLarge()) {
            extra = "  " + I18n.warnLargeRadius(radius);
        }

        String version = (String) versionBox.getSelectedItem();
        long minArea = ((Number) minAreaSpinner.getValue()).longValue();
        int maxResults = ((Number) maxResultsSpinner.getValue()).intValue();
        int threads = resolveThreads();

        SearchSettings settings = new SearchSettings(
                seed, version, radius, minArea, 16,
                refineCheck.isSelected(), shoreCheck.isSelected(),
                threads, maxResults);

        lastResults = List.of();
        tableModel.clear();
        searching = true;

        /* Immediate feedback before native work starts. */
        progressBar.setIndeterminate(true);
        progressBar.setString(I18n.initializing());
        progressBar.setEnabled(true);
        statusLabel.setText(I18n.initializing() + "  " + box + extra);
        setFormEnabled(false);
        searchBtn.setEnabled(false);
        pauseBtn.setEnabled(true);
        pauseBtn.setText(I18n.pause());
        stopBtn.setEnabled(true);
        setCursor(Cursor.getPredefinedCursor(Cursor.WAIT_CURSOR));

        /* One more EDT paint before blocking the worker thread starts. */
        javax.swing.SwingUtilities.invokeLater(() ->
                statusLabel.setText(I18n.startingThreads(threads) + "  " + box));
        runner.start(settings);
    }

    private int resolveThreads() {
        Object item = threadsBox.isEditable()
                ? threadsBox.getEditor().getItem()
                : threadsBox.getSelectedItem();
        String s = item == null ? null : String.valueOf(item).trim();
        if (s == null || s.isEmpty()
                || s.equals(I18n.auto()) || s.equalsIgnoreCase("auto")
                || s.equals("自动")) {
            return Math.max(1, Runtime.getRuntime().availableProcessors());
        }
        try {
            int n = Integer.parseInt(s);
            if (n < 1) return 1;
            if (n > 256) return 256;
            return n;
        } catch (NumberFormatException e) {
            return Math.max(1, Runtime.getRuntime().availableProcessors());
        }
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

    private void copySelected() {
        if (lastResults.isEmpty()) {
            statusLabel.setText(I18n.nothingToCopy());
            return;
        }
        int viewRow = resultTable.getSelectedRow();
        SearchResult r;
        if (viewRow < 0) {
            r = lastResults.get(0);
            if (resultTable.getRowCount() > 0) {
                resultTable.setRowSelectionInterval(0, 0);
            }
        } else {
            int modelRow = resultTable.convertRowIndexToModel(viewRow);
            r = tableModel.get(modelRow);
        }
        String text = formatCoordinate(r);
        Toolkit.getDefaultToolkit().getSystemClipboard()
                .setContents(new StringSelection(text), null);
        statusLabel.setText(I18n.copied(text));
    }

    private void setFormEnabled(boolean enabled) {
        seedField.setEnabled(enabled);
        versionBox.setEnabled(enabled);
        radiusBox.setEnabled(enabled);
        maxResultsSpinner.setEnabled(enabled);
        minAreaSpinner.setEnabled(enabled);
        refineCheck.setEnabled(enabled);
        shoreCheck.setEnabled(enabled);
        threadsBox.setEnabled(enabled);
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
            progressBar.setEnabled(false);
            setCursor(Cursor.getDefaultCursor());
        }
    }

    private void finishUiState() {
        searching = false;
        progressBar.setIndeterminate(false);
        setFormEnabled(true);
        updateActionButtons();
    }

    // ---- SearchRunner.Listener ----

    @Override
    public void onProgress(int percent, int found, String stage) {
        if (stage == null) stage = "";

        if (SearchRunner.STAGE_STOPPING.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setString(I18n.stopping());
            statusLabel.setText(I18n.stopping());
            pauseBtn.setEnabled(false);
            stopBtn.setEnabled(false);
            return;
        }
        if (SearchRunner.STAGE_PAUSED.equals(stage) || "paused".equals(stage)) {
            progressBar.setIndeterminate(false);
            if (percent >= 0) {
                progressBar.setValue(percent);
                progressBar.setString(percent + "%  ·  " + I18n.paused());
            } else {
                progressBar.setString(I18n.paused());
            }
            statusLabel.setText(I18n.paused());
            return;
        }
        if (SearchRunner.STAGE_INIT.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setString(I18n.initializing());
            statusLabel.setText(I18n.initializing());
            return;
        }
        if (SearchRunner.STAGE_STARTING.equals(stage)) {
            progressBar.setIndeterminate(true);
            progressBar.setString(I18n.startingThreads(resolveThreads()));
            statusLabel.setText(I18n.startingThreads(resolveThreads()));
            return;
        }

        String localized = localizeStage(stage);
        /* Real percent: switch to determinate. percent < 0 → indeterminate. */
        if (percent >= 0) {
            progressBar.setIndeterminate(false);
            progressBar.setValue(Math.min(100, percent));
            progressBar.setString(percent + "%  ·  " + localized);
            if (found >= 0) {
                statusLabel.setText(I18n.progressText(percent, found, localized));
            } else {
                statusLabel.setText(localized + " " + percent + "%");
            }
        } else {
            /* Native has started a stage but no reliable % yet. */
            progressBar.setIndeterminate(true);
            if (SearchRunner.STAGE_SCAN.equals(stage)) {
                progressBar.setString(I18n.scanStarted());
                statusLabel.setText(I18n.scanStarted());
            } else {
                progressBar.setString(localized);
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
        progressBar.setString("100%");
        statusLabel.setText(I18n.doneFound(results.size()));
        if (!results.isEmpty()) {
            resultTable.setRowSelectionInterval(0, 0);
        }
        finishUiState();
    }

    @Override
    public void onCancelled() {
        progressBar.setIndeterminate(false);
        statusLabel.setText(I18n.stopped());
        finishUiState();
    }

    @Override
    public void onError(String message) {
        progressBar.setIndeterminate(false);
        statusLabel.setText(I18n.errTitle() + ": " + firstLine(message));
        finishUiState();
        showLongError(message);
    }

    private static String firstLine(String s) {
        if (s == null) return "";
        int i = s.indexOf('\n');
        return i < 0 ? s : s.substring(0, i);
    }

    /** Scrollable error dialog so long native messages are not clipped. */
    private void showLongError(String message) {
        javax.swing.JTextArea area = new javax.swing.JTextArea(message == null ? "" : message);
        area.setEditable(false);
        area.setLineWrap(true);
        area.setWrapStyleWord(true);
        area.setColumns(56);
        area.setRows(Math.min(16, Math.max(6, 2 + countLines(message))));
        area.setCaretPosition(0);
        javax.swing.JScrollPane sp = new javax.swing.JScrollPane(area);
        sp.setPreferredSize(new Dimension(560, 220));
        JOptionPane.showMessageDialog(this, sp, I18n.errTitle(), JOptionPane.ERROR_MESSAGE);
    }

    private static int countLines(String s) {
        if (s == null) return 0;
        int n = 1;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) == '\n') n++;
        }
        return n;
    }

    /** Table: rank, center, area blocks, area km², span, distance. */
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

        SearchResult get(int row) {
            return data.get(row);
        }

        @Override public int getRowCount() { return data.size(); }
        @Override public int getColumnCount() { return I18n.tableColumns().length; }
        @Override public String getColumnName(int c) { return I18n.tableColumns()[c]; }

        @Override
        public Object getValueAt(int row, int col) {
            SearchResult r = data.get(row);
            return switch (col) {
                case 0 -> row + 1;
                case 1 -> r.centerX;
                case 2 -> r.centerZ;
                case 3 -> r.areaBlocksFormatted();
                case 4 -> r.areaKm2Formatted();
                case 5 -> r.width + " × " + r.height;
                case 6 -> (long) r.distance;
                default -> "";
            };
        }
    }
}
