export const metadata = {
  title: "Závlaha",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="cs">
      <body style={{ margin: 0 }}>{children}</body>
    </html>
  );
}
