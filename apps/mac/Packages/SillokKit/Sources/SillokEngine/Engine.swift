import CSillok

public enum Engine {
    public static var version: String {
        String(cString: sillok_version())
    }
}
