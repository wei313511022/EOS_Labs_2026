import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Overbug | RTES913: EOS",
  description:
    "Raspberry Pi operating-systems labs built around the cooperative Overbug repair game.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
