package dev.sakuhime.mushroomfinder;

import javax.swing.SwingUtilities;
import javax.swing.UIManager;

/** Application entry point. */
public final class Main {
    public static void main(String[] args) {
        // Headless smoke test: --self-test
        if (args.length > 0 && "--self-test".equals(args[0])) {
            int code = SelfTest.run();
            System.exit(code);
        }

        SwingUtilities.invokeLater(() -> {
            try {
                UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
            } catch (Exception ignored) {
            }
            MainFrame frame = new MainFrame();
            frame.setVisible(true);
        });
    }

    private Main() {}
}
